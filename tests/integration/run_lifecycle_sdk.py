#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify native application, service and job owners through a relocated SDK.
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
ROOT=Path(__file__).resolve().parents[2]


def run(args):
    """Exercise launch, quit negotiation, script preconditions and supervision."""
    evidence=args.evidence.resolve()
    evidence.mkdir(parents=True,exist_ok=True)
    history=[]
    with tempfile.TemporaryDirectory(prefix='stomme-lifecycle-') as directory:
        work=Path(directory)
        sdk=work/'sdk'
        shutil.copytree(args.sdk.resolve(),sdk)
        for name,source in [('client','tests/integration/shell_resources_client.c'),('stop','examples/resource_client.c'),('fixture','tests/integration/window_resources_fixture.c')]:
            shutil.copy2(ROOT/source,work/(name+'.c'))
        (work/'Makefile').write_text('include '+str(sdk/'sdk.mk')+'\nKITS := $(foreach kit,$(SDK_KIT_NAMES),$(SDK_ROOT)/lib/libtiku_$(kit).a)\nall: client stop Fixture.so\n%: %.c\n\t$(CC) -Wall -Wextra -Werror -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\nFixture.so: fixture.c\n\t$(CC) -Wall -Wextra -Werror -shared -fPIC -DTIKU_APP_SO -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\n')
        built=subprocess.run(['make','-j3'],cwd=work,capture_output=True,text=True)
        (evidence/'sdk-build.log').write_text(built.stdout+built.stderr)
        assert not built.returncode,built.stderr
        home=work/'home'
        (home/'Desktop').mkdir(parents=True)
        fixture_data=work/'fixture-data'
        fixture_data.mkdir()
        desktop=args.desktop.resolve()
        apps=work/'apps'
        bundle=apps/'fixture'
        bundle.mkdir(parents=True)
        for native in (desktop.parent.parent/'apps').iterdir():
            if native.is_dir() and (native/'app.manifest').is_file():
                (apps/native.name).symlink_to(native,target_is_directory=True)
        shutil.copy2(work/'Fixture.so',bundle/'Fixture.so')
        (bundle/'app.manifest').write_text('version=1\nid=fixture\nname=Window fixture\nmodule=Fixture.so\ncategory=utilities\nicon=text\ntypes=text/plain\nstartup=apps/Fixture.so\nhelp=apps/fixture.html\n')
        source=home/'Desktop/source.txt'
        source.write_text('operation cancellation fixture\n')
        destination=home/'Desktop/Destination'
        destination.mkdir()
        (destination/source.name).write_text('existing destination\n')
        control,driver=work/'control',work/'driver.sock'
        env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home/'.config'),XDG_DATA_HOME=str(home/'.local/share'),XDG_CACHE_HOME=str(home/'.cache'),TIKU_APPS=str(apps),STOMME_RESOURCE_DIRECTORY=str(control),STOMME_RESOURCE_TEST_PROVIDER='1',STOMME_RESOURCE_MOUNT='/lifecycle-'+str(os.getpid()),STOMME_WINDOW_FIXTURE_DIRECTORY=str(fixture_data))
        env.pop('DISPLAY',None)
        desktop=args.desktop.resolve()
        log=(evidence/'desktop.log').open('w')
        process=None
        peer=None

        def call(operation,path,*arguments,success=True):
            """Retain independent SDK request outcomes and literal arguments."""
            for attempt in range(12):
                result=subprocess.run([str(work/'client'),str(control),operation,str(path),*map(str,arguments)],capture_output=True,text=True,timeout=20)
                history.append(dict(operation=operation,path=str(path),arguments=arguments,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
                if success and operation=='record' and result.returncode and result.stderr.strip() in ['stale_revision','busy','settings job: failed busy none','settings job: failed stale_revision none']:
                    time.sleep(.1)
                    continue
                assert (result.returncode==0)==success,history[-1]
                return result.stdout.strip()
            raise AssertionError(('record owner remained busy',path,history[-1]))

        def read(path):
            """Read one copied public observation."""
            return json.loads(call('read',path))['applied']

        def children(path):
            """List complete child identities through cursor enumeration."""
            for attempt in range(8):
                try:return [line.split('\t')[0] for line in call('list',path).splitlines()]
                except AssertionError:
                    if not history[-1]['stderr'].startswith('stale_cursor'):raise
                    time.sleep(.08)
            raise AssertionError(('unstable enumeration',path))

        def stream(path):
            """Read a complete frozen NDJSON stream."""
            for attempt in range(8):
                try:return [json.loads(line) for line in call('stream',path).splitlines()]
                except AssertionError:
                    if history[-1]['stderr'].strip()!='stale_revision':raise
                    time.sleep(.08)
            raise AssertionError(('unstable stream precondition',path))

        def action(path,*arguments):
            """Retry only requests refused before dispatch by revision contention."""
            for attempt in range(8):
                result=subprocess.run([str(work/'client'),str(control),'action',path,*map(str,arguments)],capture_output=True,text=True,timeout=20)
                history.append(dict(operation='action',path=path,arguments=arguments,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
                if not result.returncode:return
                assert 'stale' in result.stderr or 'busy' in result.stderr,history[-1]
                time.sleep(.1)
            raise AssertionError(history[-1])

        def instance():
            """Find the connected fixture through its verified manifest."""
            for path in children('/apps'):
                if path=='/apps/catalog':continue
                try:
                    value=read(path+'/manifest')
                    if value.get('verified_child') and value.get('id')=='fixture' and read(path+'/state')['phase']=='connected':return path
                except AssertionError:
                    if not history[-1]['stderr'].startswith('not_found'):raise
            return None

        def script(verb,property='entry',specifier='direct',*extra,state=None,success=True):
            """Submit a native file-manager verb with an observed ordering token."""
            state=state or read('/desktop/file-manager/state')
            arguments=('script',state['window'],state['generation'],state['order_token'],property,specifier,*extra)
            path='/desktop/file-manager/actions/TIKU_TRK_VERB_'+verb
            if success:action(path,*arguments)
            else:call('action',path,*arguments,success=False)
            time.sleep(.3)
            return state

        def start(supervised=False):
            """Start a fresh isolated desktop and its native event driver."""
            nonlocal process,peer
            command=[str(desktop),'-conduct',str(driver),str(home/'Desktop')]
            if supervised:command=[str(desktop.parent/'tiku-desk'),'-conduct',str(driver),str(home/'Desktop')]
            process=subprocess.Popen(command,cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+30
            while not (control/'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic()<deadline,'desktop startup failed'
                time.sleep(.02)
            peer=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer,1,struct.pack('=I',2))
            time.sleep(.5)

        def stop():
            """Request test-provider shutdown and require native endpoint cleanup."""
            nonlocal peer
            if peer:peer.close();peer=None
            result=subprocess.run([str(work/'stop'),str(control),'--stop'],capture_output=True,text=True,timeout=20)
            assert not result.returncode,result.stderr
            assert process.wait(timeout=25)==0
            assert not (control/'control.sock').exists()

        try:
            start()
            assert not read('/services/supervisor/identity')['available']
            assert read('/services/lifecycle')['registration_complete']
            manifests=stream('/apps/catalog/manifests')
            assert any(row['id']=='fixture' for row in manifests),manifests
            bindings=[]
            for row in manifests:
                if row['id'] and not row['native_desktop']:
                    bindings.extend((f'/apps/catalog/{row["id"]}/actions/{name}','action') for name in ['launch','open'])
            for name in ['manifests','capabilities','types','associations']:bindings.append(('/apps/catalog/'+name,'stream'))
            for name in ['identity','state','child','last-exit','restart-count']:bindings.append(('/services/supervisor/'+name,'state'))
            for name in ['identity','state']:bindings.append(('/services/runner/'+name,'state'))
            for name in ['applications','failures']:bindings.append(('/services/runner/'+name,'stream'))
            for name in ['owner','pending','status']:bindings.append(('/notifications/'+name,'state'))
            for name in ['request-end','request-quit','restore']:bindings.append(('/session/actions/'+name,'action'))
            for name in ['GET','SET','COUNT','CREATE','DELETE','EXECUTE']:bindings.append(('/desktop/file-manager/actions/TIKU_TRK_VERB_'+name,'action'))
            audit=evidence/'catalog-coverage.tsv'
            audit.write_text(''.join(path+'\t'+kind+'\n' for path,kind in bindings))
            assert call('audit',audit)==str(len(bindings))+' catalog bindings verified'
            (evidence/'loaded-manifests.json').write_text(json.dumps(manifests,indent=2)+'\n')
            assert stream('/apps/catalog/capabilities')
            stream('/apps/catalog/types')
            stream('/apps/catalog/associations')
            call('action','/apps/catalog/fixture/actions/open',success=False)
            assert 'invalid_value' in history[-1]['stderr']
            call('action','/apps/catalog/fixture/actions/open','document','relative document.txt',success=False)
            assert 'invalid_value' in history[-1]['stderr']
            call('action','/apps/catalog/fixture/actions/launch','document',str(source),success=False)
            assert 'invalid_value' in history[-1]['stderr']
            if any(row['id']=='calc' for row in manifests):
                call('action','/apps/catalog/calc/actions/open','document',str(source),success=False)
                assert 'unsupported' in history[-1]['stderr']
            action('/apps/catalog/fixture/actions/launch')
            gui.wait_for(lambda:instance() is not None,True)
            base=instance()
            for field in ['identity','manifest','capabilities','windows','state']:read(base+'/'+field)
            app_audit=evidence/'instance-coverage.tsv'
            app_audit.write_text(''.join(base+'/'+field+'\tstate\n' for field in ['identity','manifest','capabilities','windows','state'])+''.join(base+'/actions/'+name+'\taction\n' for name in ['activate','request-quit']))
            assert call('audit',app_audit)=='7 catalog bindings verified'
            assert read(base+'/capabilities')['request_quit']
            action(base+'/actions/activate')
            gui.wait_for(lambda:gui.query(peer,'title'),'Unsaved fixture')
            action(base+'/actions/request-quit')
            gui.wait_for(lambda:int((fixture_data/'state').read_text().split()[0])>0,True)
            assert read(base+'/state')['running'],'quit bypassed application veto'
            (fixture_data/'allow-close').write_text('consent\n')
            action(base+'/actions/request-quit')
            gui.wait_for(lambda:base not in children('/apps'),True)
            call('action',base+'/actions/activate',success=False)
            document=work/'literal space document.txt'
            document.write_text('native document\n')
            action('/apps/catalog/fixture/actions/open','document',document)
            gui.wait_for(lambda:(fixture_data/'document').exists(),True)
            assert (fixture_data/'document').read_text()==str(document)
            gui.wait_for(lambda:instance() is not None,True)
            replacement=instance()
            assert replacement!=base
            call('action',base+'/actions/request-quit',success=False)
            action(replacement+'/actions/request-quit')
            gui.wait_for(lambda:replacement not in children('/apps'),True)
            gui.wait_for(lambda:read('/desktop/file-manager/state')['available'],True)
            script('GET','path')
            script('COUNT')
            state=script('SET','selection','path','path',str(source))
            script('COUNT','selection')
            script('GET',state=state,success=False)
            script('SET','selection','index',success=False)
            script('CREATE','selection','path','path',str(destination))
            script('DELETE','selection')
            selected=script('SET','selection','path','path',str(source))
            action('/windows/'+selected['window']+'/actions/focus')
            gui.wait_for(lambda:read('/input/focus')['window'],selected['window'])
            assert source.is_file() and destination.is_dir()
            gui.key(peer,ord('c'),2)
            script('EXECUTE','entry','path','path',str(destination))
            gui.wait_for(lambda:read('/desktop/file-manager/state').get('path'),str(destination))
            destination_window=read('/desktop/file-manager/state')['window']
            action('/windows/'+destination_window+'/actions/focus')
            gui.wait_for(lambda:read('/input/focus')['window'],destination_window)
            gui.key(peer,ord('v'),2)
            def file_job():
                """Find the native file engine's current generation."""
                for path in children('/jobs'):
                    try:
                        if read(path+'/identity').get('kind')=='filesystem':return path
                    except AssertionError:
                        if not history[-1]['stderr'].startswith('not_found'):raise
                return None
            gui.wait_for(lambda:file_job() is not None,True)
            job=file_job()
            gui.wait_for(lambda:read(job+'/state')['phase'],'awaiting_confirmation')
            for field in ['identity','owner','state','progress','cancellation','error','result']:read(job+'/'+field)
            action(job+'/actions/cancel')
            gui.wait_for(lambda:read(job+'/state')['phase'],'cancelled')
            assert read(job+'/result')['cancelled']
            job_audit=evidence/'job-coverage.tsv'
            job_audit.write_text(''.join(job+'/'+field+'\tstate\n' for field in ['identity','owner','state','progress','cancellation','error','result'])+job+'/actions/cancel\taction\n')
            assert call('audit',job_audit)=='8 catalog bindings verified'
            assert (destination/source.name).read_text()=='existing destination\n'
            gui.key(peer,ord('v'),2)
            gui.wait_for(lambda:file_job()!=job,True)
            newer=file_job()
            call('action',job+'/actions/cancel',success=False)
            action(newer+'/actions/cancel')
            for path in children('/jobs'):
                try:
                    if read(path+'/identity').get('kind')=='worker':
                        assert not read(path+'/cancellation')['supported']
                        call('action',path+'/actions/cancel',success=False)
                        break
                except AssertionError:
                    if not history[-1]['stderr'].startswith('not_found'):raise
            for record in children('/session/records/session'):
                if read(record+'/node')['node']=='\x01session':
                    call('record',record+'/value','session',destination)
                    break
            else:raise AssertionError('session record missing')
            folder_window=read('/desktop/file-manager/state')['window']
            action('/windows/'+folder_window+'/actions/request-close')
            gui.wait_for(lambda:read('/desktop/file-manager/state').get('path')!=str(destination),True)
            action('/session/actions/restore')
            gui.wait_for(lambda:read('/desktop/file-manager/state').get('path'),str(destination))
            for name in ['request-end','request-quit']:
                action('/session/actions/'+name)
                gui.wait_for(lambda:read('/notifications/pending')['open'],True)
                gui.key(peer,11)
                gui.wait_for(lambda:not read('/notifications/pending')['open'],True)
                assert process.poll() is None
            stop()
            env['STOMME_LIFECYCLE_FAILURE']='1'
            start()
            action('/apps/catalog/fixture/actions/launch')
            gui.wait_for(lambda:bool(stream('/services/runner/failures')),True)
            failure=stream('/services/runner/failures')[-1]
            assert failure['app_id']=='fixture' and failure['status'] and Path(failure['report']).is_file(),failure
            stop()
            env.pop('STOMME_LIFECYCLE_FAILURE')
            start(supervised=True)
            supervisor=read('/services/supervisor/identity')
            assert supervisor['available'] and supervisor['pid']==process.pid,supervisor
            child=read('/services/supervisor/child')['pid']
            assert read('/services/supervisor/restart-count')['count']==0
            peer.close();peer=None
            os.kill(child,signal.SIGKILL)
            def restarted():
                """Tolerate endpoint unavailability while the owned child is replaced."""
                try:return read('/services/supervisor/child')['pid']!=child
                except (AssertionError,KeyError,json.JSONDecodeError):return False
            gui.wait_for(restarted,True)
            assert read('/services/supervisor/restart-count')['count']==1
            previous=read('/services/supervisor/last-exit')
            assert previous['known'] and previous['signal']==signal.SIGKILL,previous
            stop()
            report=dict(status='passed',sdk_identity=json.loads((sdk/'sdk.json').read_text())['sdk_identity'],relocated_sdk=True,launch_open=True,verified_instance_identity=True,quit_veto_and_consent=True,stale_instance_rejected=True,native_script_verbs=6,stale_order_rejected=True,file_generation_cancellation=True,runner_failure_report=True,supervisor_restart=True,session_confirmation=True,endpoint_cleanup=True,hardware_ports_opened=False)
            (evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
        finally:
            reports=home/'.config/tracker/reports'
            if reports.exists():shutil.copytree(reports,evidence/'reports',dirs_exist_ok=True)
            (evidence/'history.json').write_text(json.dumps(history,indent=2,default=str)+'\n')
            if peer:peer.close()
            if process and process.poll() is None:process.terminate();process.wait(timeout=15)
            log.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--desktop',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    run(parser.parse_args())
