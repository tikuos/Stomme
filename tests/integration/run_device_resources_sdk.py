#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Qualify product device policy and simulated board requests through an SDK."""
from pathlib import Path
import argparse
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time
import run_settings_sdk as gui
ROOT = Path(__file__).resolve().parents[2]


def run(args):
    """Run a native desktop followed by the optional simulated board producer."""
    evidence=args.evidence.resolve();evidence.mkdir(parents=True,exist_ok=True)
    history=[]
    with tempfile.TemporaryDirectory(prefix='stomme-devices-') as directory:
        work=Path(directory);sdk=work/'sdk';shutil.copytree(args.sdk.resolve(),sdk)
        shutil.copy2(ROOT/'tests/integration/shell_resources_client.c',work/'shell_resources_client.c')
        source=work/'shell_resources_client.c'
        if args.fixture_client:
            shutil.copy2(args.fixture_client.resolve(),work/'client.c');source=work/'client.c'
        built=subprocess.run(['cc','-D_DEFAULT_SOURCE','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(sdk/'include'),str(source),str(sdk/'lib/libtiku_application.a'),str(sdk/'lib/libtiku_support.a'),'-lm','-pthread','-o',str(work/'client')],capture_output=True,text=True)
        (evidence/'sdk-build.log').write_text(built.stdout+built.stderr);assert not built.returncode,built.stderr
        home=work/'home';(home/'Desktop').mkdir(parents=True)
        control=work/'control';driver=work/'driver.sock'
        env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home/'.config'),XDG_DATA_HOME=str(home/'.local/share'),XDG_STATE_HOME=str(home/'.local/state'),XDG_CACHE_HOME=str(home/'.cache'),STOMME_RESOURCE_DIRECTORY=str(control),STOMME_RESOURCE_MOUNT='/device-sdk',TIKU_APPS=str(args.apps.resolve()))
        env.pop('DISPLAY',None)

        def call(op,path,*values,success=True):
            """Keep external requests and retry known contention before effects."""
            for attempt in range(30):
                result=subprocess.run([str(work/'client'),str(control),op,str(path),*map(str,values)],env=env,capture_output=True,text=True,timeout=20)
                history.append(dict(operation=op,path=str(path),values=values,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
                if success and result.returncode and result.stderr.strip() in ['busy','stale_revision','stale_cursor']:
                    time.sleep(.05);continue
                assert (result.returncode==0)==success,history[-1]
                return result.stdout.strip()
            raise AssertionError(history[-1])

        def read(path):
            """Decode the applied public snapshot without native model headers."""
            result=call('read',path)
            applied=result.split(',\"applied\":',1)[1].rsplit('\n}',1)[0].rstrip('\n')
            try:return json.loads(applied)
            except json.JSONDecodeError:return applied

        def wait(function):
            """Wait for an asynchronous publication with a bounded deadline."""
            deadline=time.monotonic()+25
            while time.monotonic()<deadline:
                value=function()
                if value:return value
                time.sleep(.05)
            raise AssertionError(('publication timeout',history[-5:]))

        def children(path):
            """Consume the complete child catalog from another SDK process."""
            return [line.split('\t')[0] for line in call('list',path).splitlines()]

        log=(evidence/'desktop.log').open('w')
        process=subprocess.Popen([str(args.desktop.resolve()),'-conduct',str(driver)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);peer=None
        try:
            wait(lambda:(control/'control.sock').exists() and driver.exists() and process.poll() is None)
            peer=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);peer.settimeout(5);peer.connect(str(driver));gui.send(peer,1,struct.pack('=I',2))
            capability=read('/devices/capabilities');assert capability['supported']==args.boards and not capability['automatic_port_probes']
            if args.boards:
                assert read('/devices/discovery/state')['candidates']==0
                assert call('stream','/devices/discovery/candidates')==''
                for key in ['currentdevice','currentdeviceport']:
                    path='/settings/tracker/'+key;assert read(path)==''
                    call('record',path,'object',success=False)
                for manifest,kind in [('bench','build'),('basic','debug'),('c','debug')]:
                    call('action','/apps/catalog/'+manifest+'/actions/launch')
                    def find_provider():
                        for base in children('/apps'):
                            if base=='/apps/catalog':continue
                            if base+'/development' not in children(base):continue
                            if read(base+'/development/kind')!=kind:continue
                            identity=read(base+'/identity')
                            if (manifest=='bench' and identity.get('id')=='tiku.bench') or identity.get('id','').endswith('.tiku'+manifest):return base
                        return None
                    base=wait(find_provider);assert read(base+'/development/target')==''
                    state=read(base+'/development/state');assert state['commands']=='commands/invoke' and state['confirmation']=='native_application_policy'
                    assert base+'/commands/invoke' in children(base+'/commands')
            else:
                call('read','/devices/discovery/state',success=False)
                for key in ['currentdevice','currentdeviceport']:call('read','/settings/tracker/'+key,success=False)
                catalog=children('/apps/catalog');assert all('/apps/catalog/'+name not in catalog for name in ['bench','basic','c'])
            (evidence/'policy.json').write_text(json.dumps(dict(board_services=args.boards,hardware_ports_opened=False,development_providers=3 if args.boards else 0),indent=2)+'\n')
        finally:
            if peer:peer.close()
            if process.poll() is None:
                os.killpg(process.pid,signal.SIGTERM)
                try:process.wait(timeout=8)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
            try:os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            log.close()
            (evidence/'requests.json').write_text(json.dumps(history,indent=2)+'\n')
        if args.fixture:
            shutil.rmtree(control,ignore_errors=True)
            state_dir=work/'fixture';state_dir.mkdir();log=(evidence/'fixture.log').open('w')
            process=subprocess.Popen([str(args.fixture.resolve()),str(control),str(state_dir)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                wait(lambda:(control/'control.sock').exists() and (state_dir/'state').exists() and process.poll() is None)
                def record():
                    for base in children('/devices'):
                        if base in ['/devices/discovery','/devices/capabilities']:continue
                        if base+'/configuration/status' in children(base+'/configuration') and read(base+'/configuration/status')['sampled']:return base
                    return None
                base=wait(record);path=base+'/configuration/name'
                for family in ['identity','connection','mounts','configuration','stats','build-jobs','debug-sessions']:assert base+'/'+family in children(base)
                assert read(base+'/mounts/namespace')['path']=='/devices/sensor'
                assert read(base+'/identity/value')['uid']=='0102030405060708'
                connection=read(base+'/connection/value')
                assert connection['mounted'] and connection['uid_verified'] and connection['port']=='simulated-port'
                stats=read(base+'/stats/sample')
                assert stats['sampled'] and stats['uptime_known'] and stats['uptime']=='100'
                assert stats['memory_free_known'] and stats['memory_used_known'] and stats['cpu_frequency_known']
                audit=work/'collections.tsv'
                audit.write_text(''.join(base+'/'+family+'\tcollection\n' for family in ['identity','connection','mounts','configuration','stats','build-jobs','debug-sessions']))
                assert call('audit',audit)=='7 catalog bindings verified'
                for family in ['build-jobs','debug-sessions']:
                    rows=[json.loads(row) for row in call('stream',base+'/'+family+'/sources').splitlines()]
                    assert rows[-1]['kind']=='catalog_status' and rows[-1]['authority']=='native_application_providers'
                def command(text):
                    temporary=state_dir/'next-command';temporary.write_text(text+'\n');temporary.replace(state_dir/'command');wait(lambda:not (state_dir/'command').exists())
                def effects():return (state_dir/'state').read_text().splitlines()
                def queue(text):
                    value=wait(lambda:(snapshot if len(snapshot.get('context',''))==64 else None) if isinstance(snapshot:=read(path),dict) else None)
                    return call('queue',path,text,value['context'])
                def terminal(job,code):
                    def done():
                        state=call('job-status',job).split('\t');return state if state[0] not in ['queued','running'] else None
                    result=wait(done);assert result[1]==code,result;return result
                job=queue('SDK sensor');terminal(job,'ok');assert effects()[3]=='SDK sensor'
                command('hold');before=effects()[0];job=queue('Cancelled SDK');wait(lambda:call('job-status',job).startswith('running'))
                call('cancel',job);command('release');terminal(job,'cancelled');assert effects()[0]==before
                command('hold');job=queue('Wrong UID');wait(lambda:call('job-status',job).startswith('running'));command('swap-uid');command('release');terminal(job,'owner_lost');assert effects()[0]==before
                command('restore-uid');wait(lambda:read(base+'/configuration/status')['name_writable']==1)
                command('hold');job=queue('Wrong mount');wait(lambda:call('job-status',job).startswith('running'));command('replug');command('release');terminal(job,'unknown_outcome');assert effects()[0]==before
                call('read',path,success=False);old_base=base;base=wait(record);assert base!=old_base
                path=base+'/configuration/name';command('managed');env['STOMME_TEST_DURABILITY']='user'
                wait(lambda:read(base+'/configuration/status')['name_managed']==1)
                before_plain=effects()[1];job=queue('Managed SDK');terminal(job,'ok');assert effects()[3]=='Managed SDK' and effects()[1]==before_plain
                command('stop');process.wait(timeout=8);assert process.returncode==0
                (evidence/'simulation.json').write_text(json.dumps(dict(collections=7,save=True,cancel=True,uid_mismatch=True,mount_replacement=True,managed_outbox=True,hardware=False),indent=2)+'\n')
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=8)
                log.close()
                (evidence/'requests.json').write_text(json.dumps(history,indent=2)+'\n')
        (evidence/'requests.json').write_text(json.dumps(history,indent=2)+'\n')
        print('PASS device SDK: native product policy'+(', simulated identity/configuration/cancellation' if args.fixture else ''))


def main():
    """Select exported artifacts; the simulated producer stays product-owned."""
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['sdk','desktop','apps','evidence']:parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--boards',action='store_true');parser.add_argument('--fixture',type=Path);parser.add_argument('--fixture-client',type=Path)
    run(parser.parse_args())

if __name__=='__main__':main()
