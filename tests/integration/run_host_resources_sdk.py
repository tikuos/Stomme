#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify native service adapters through a relocated SDK and isolated desktop.
from pathlib import Path
import argparse
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import run_settings_sdk as gui
ROOT=Path(__file__).resolve().parents[2]


def run(args):
    """Verify native facts, content generations, semantic dispatch and grant ownership."""
    evidence=args.evidence.resolve()
    evidence.mkdir(parents=True,exist_ok=True)
    history=[]
    with tempfile.TemporaryDirectory(prefix='stomme-host-') as directory:
        work=Path(directory)
        sdk=work/'sdk'
        shutil.copytree(args.sdk.resolve(),sdk)
        for name,source in [('client','shell_resources_client.c'),('fixture','host_resources_fixture.c')]:
            shutil.copy2(ROOT/'tests/integration'/source,work/(name+'.c'))
        shutil.copy2(ROOT/'examples/resource_client.c',work/'stop.c')
        (work/'Makefile').write_text('include '+str(sdk/'sdk.mk')+'\nKITS := $(foreach kit,$(SDK_KIT_NAMES),$(SDK_ROOT)/lib/libtiku_$(kit).a)\nall: client stop Fixture.so\n%: %.c\n\t$(CC) -Wall -Wextra -Werror -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\nFixture.so: fixture.c\n\t$(CC) -Wall -Wextra -Werror -shared -fPIC -DTIKU_APP_SO -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\n')
        built=subprocess.run(['make','-j3'],cwd=work,capture_output=True,text=True)
        (evidence/'sdk-build.log').write_text(built.stdout+built.stderr)
        assert not built.returncode,built.stderr
        home=work/'home'
        (home/'Desktop').mkdir(parents=True)
        fixture_data=work/'fixture-data'
        fixture_data.mkdir()
        apps=work/'apps'
        bundle=apps/'fixture'
        bundle.mkdir(parents=True)
        shutil.copy2(work/'Fixture.so',bundle/'Fixture.so')
        (bundle/'app.manifest').write_text('version=1\nid=fixture\nname=Host fixture\nmodule=Fixture.so\ncategory=utilities\nicon=text\nstartup=apps/Fixture.so\nhelp=apps/fixture.html\n')
        control,driver=work/'control',work/'driver.sock'
        env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home/'.config'),XDG_DATA_HOME=str(home/'.local/share'),XDG_CACHE_HOME=str(home/'.cache'),TIKU_APPS=str(apps),STOMME_RESOURCE_DIRECTORY=str(control),STOMME_RESOURCE_TEST_PROVIDER='1',STOMME_RESOURCE_MOUNT='/host-'+str(os.getpid()),STOMME_HOST_FIXTURE_DIRECTORY=str(fixture_data))
        env.pop('DISPLAY',None)
        log=(evidence/'desktop.log').open('w')
        process=None
        peer=None

        def call(operation,path,*arguments,success=True):
            """Retain the exact requests and native protocol outcomes."""
            result=subprocess.run([str(work/'client'),str(control),operation,str(path),*map(str,arguments)],capture_output=True,text=True,timeout=20)
            history.append(dict(operation=operation,path=str(path),arguments=arguments,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
            assert (result.returncode==0)==success,history[-1]
            return result.stdout.strip()

        def read(path):
            """Read one immutable public observation."""
            return json.loads(call('read',path))['applied']

        def copied(operation,path):
            """Restart a read-only collection after explicit stale preconditions."""
            for attempt in range(12):
                try:return call(operation,path)
                except AssertionError:
                    if history[-1]['stderr'].strip() not in ['stale_cursor','stale_revision']:raise
                    time.sleep(.08)
            raise AssertionError(('unstable collection',path))

        def children(path):
            """Discover complete immediate children through the negotiated page size."""
            return [line.split('\t')[0] for line in copied('list',path).splitlines()]

        def stream(path):
            """Read a complete frozen native stream."""
            return [json.loads(line) for line in copied('stream',path).splitlines()]

        def fields(values):
            """Encode a typed object without private C model layouts."""
            out=['object']
            for key,value in values.items():out.extend([('i:' if isinstance(value,int) else 't:')+key,str(value)])
            return out

        def action(path,values=None,success=True):
            """Retry only pre-dispatch revision refusals; preserve other outcomes."""
            arguments=fields(values or {})
            if not success:return call('action-result',path,*arguments,success=False)
            for attempt in range(12):
                try:return json.loads(call('action-result',path,*arguments))
                except AssertionError:
                    if history[-1]['stderr'].strip() not in ['stale_revision','busy']:raise
                    time.sleep(.08)
            raise AssertionError(history[-1])

        def wait(test,wanted=True):
            """Wait for an owner publication using the shared workflow deadline."""
            return gui.wait_for(test,wanted)

        try:
            process=subprocess.Popen([str(args.desktop.resolve()),'-mute','-conduct',str(driver),str(home/'Desktop')],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+30
            while not (control/'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic()<deadline,'desktop startup failed'
                time.sleep(.02)
            peer=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer,1,struct.pack('=I',2))
            time.sleep(.7)
            display=children('/display')
            assert len(display)==1,display
            bindings=[]
            for name in ['identity','host','geometry','scale','capabilities']:bindings.append((display[0]+'/'+name,'state'))
            for base,names in [('/input',['capabilities','focus','keymap','modifiers','pointer']),('/audio',['availability','playback','volume']),('/clipboard',['availability','generation','owner','types']),('/metrics',['drops','jobs','provider-count','queue-depth','session-count']),('/printers/native',['identity','state','capabilities'])]:
                bindings.extend((base+'/'+name,'state') for name in names)
            bindings.extend(('/accessibility/'+name,'stream' if name in ['applications','generations','trees'] else 'state') for name in ['applications','capabilities','generations','trees'])
            bindings.append(('/printers/native/jobs','stream'))
            for path in ['/clipboard/actions/read-content','/clipboard/actions/publish-content','/accessibility/actions/perform-action','/printers/native/actions/submit','/printers/native/actions/cancel']:bindings.append((path,'action'))
            wait(lambda:read('/volumes/status')['available'])
            volumes=[path for path in children('/volumes') if path!='/volumes/status']
            assert volumes,read('/volumes/status')
            for name in ['capacity','flags','free-space','identity','mounts','state']:bindings.append((volumes[0]+'/'+name,'state'))
            bindings.extend((volumes[0]+'/actions/'+name,'action') for name in ['eject','unmount'])
            assert len(bindings)==43,len(bindings)
            coverage=evidence/'coverage.tsv'
            coverage.write_text(''.join(path+'\t'+kind+'\n' for path,kind in bindings))
            assert call('audit',coverage)=='43 catalog bindings verified'
            assert not read(display[0]+'/host')['native_available']
            assert not read(display[0]+'/scale')['known']
            assert read(display[0]+'/geometry')['width']>0
            assert not read('/input/keymap')['native_layout_known']
            assert read('/metrics/provider-count')['capacity']==512
            assert read('/metrics/session-count')['resource_connections']>=1
            assert not read('/metrics/drops')['native_input_drop_counter']
            assert read('/audio/volume')['percent']==read('/settings/sound/volume')
            gui.inject(peer,8,x=143,y=201,mods=1)
            wait(lambda:read('/input/pointer')['x']==143 and read('/input/pointer')['y']==201)
            wait(lambda:read('/input/modifiers')['value']==1)
            assert not read('/input/capabilities')['vfs_input_injection']
            generation=read('/clipboard/generation')['token']
            payload=bytes(range(256))*12
            result=action('/clipboard/actions/publish-content',dict(generation=generation,text='SDK clipboard é中😀',mime='application/x-stomme-fixture',data_base64=base64.b64encode(payload).decode()))
            current=result['generation']
            assert current!=generation
            wait(lambda:read('/clipboard/generation')['token']==current)
            assert not read('/clipboard/owner')['publisher_known']
            assert read('/clipboard/types')['typed_count']==1
            content=b''
            while len(content)<len(payload):
                chunk=action('/clipboard/actions/read-content',dict(generation=current,representation='typed',mime='application/x-stomme-fixture',offset=len(content),maximum=2048))
                assert chunk['generation']==current and chunk['offset']==len(content)
                content+=base64.b64decode(chunk['data'],validate=True)
                assert chunk['next_offset']==len(content)
            assert content==payload and chunk['complete']
            text=action('/clipboard/actions/read-content',dict(generation=current,representation='text',offset=0,maximum=2048))
            assert base64.b64decode(text['data']).decode()=='SDK clipboard é中😀'
            action('/clipboard/actions/publish-content',dict(generation=generation,text='stale'),success=False)
            assert 'stale_revision' in history[-1]['stderr']
            action('/clipboard/actions/publish-content',dict(generation=current,text='bad',mime='application/x-stomme-fixture',data_base64='YR=='),success=False)
            assert 'invalid_value' in history[-1]['stderr']
            for volume in volumes:
                assert read(volume+'/identity')['identity_scope']=='observed_native_mount_record'
                assert not read(volume+'/state')['atomic_mount_incarnation']
                action(volume+'/actions/eject',success=False)
                assert 'unsupported' in history[-1]['stderr']
                if read(volume+'/flags')['boot']:
                    action(volume+'/actions/unmount',success=False)
                    assert 'unsupported' in history[-1]['stderr']
            semantic=read('/accessibility/capabilities')['remote_semantic_publications']
            assert read('/printers/native/capabilities')['cancel_supported']==semantic
            action('/apps/catalog/fixture/actions/launch')
            wait(lambda:gui.query(peer,'title'),'Host services fixture')
            if semantic:
                wait(lambda:len(stream('/accessibility/generations'))==1)
                publication=stream('/accessibility/generations')[0]
                assert publication['document']=='101' and publication['generation']=='1' and publication['nodes']=='1',publication
                rows=stream('/accessibility/trees')
                packet=b''.join(base64.b64decode(row['data'],validate=True) for row in rows)
                assert packet==(fixture_data/'tree-1.bin').read_bytes()
                assert rows[-1]['complete'] and stream('/accessibility/applications')[0]['window']==publication['window']
                request=dict(window=publication['window'],document='101',generation='1',node=0,action='select',first=1,last=3)
                outcome=action('/accessibility/actions/perform-action',request)
                assert not outcome['input_gesture_granted'] and not outcome['application_completion_known']
                wait(lambda:(fixture_data/'actions').exists())
                wait(lambda:stream('/accessibility/generations')[0]['generation'],'2')
                action('/accessibility/actions/perform-action',request,success=False)
                assert 'stale_revision' in history[-1]['stderr']
                request['generation']='2'
                request['last']=5
                action('/accessibility/actions/perform-action',request,success=False)
                assert 'invalid_value' in history[-1]['stderr']
                gui.key(peer,ord('p'),2)
                wait(lambda:(fixture_data/'print-ready').exists())
                assert (fixture_data/'print-ready').read_text().strip()=='1'
                wait(lambda:len(stream('/printers/native/jobs'))==1)
                job=stream('/printers/native/jobs')[0]
                grant={key:job[key] for key in ['window','ticket','connection','incarnation']}
                wrong=dict(grant,connection=str(int(grant['connection'])+1))
                action('/printers/native/actions/cancel',wrong,success=False)
                assert 'owner_lost' in history[-1]['stderr']
                action('/printers/native/actions/submit',dict(grant,pdf_base64=base64.b64encode(b'%PDF-fixture').decode()),success=False)
                assert 'unavailable' in history[-1]['stderr']
                assert len(stream('/printers/native/jobs'))==1
                assert action('/printers/native/actions/cancel',grant)['accepted']
                wait(lambda:(fixture_data/'print-done').exists())
                assert (fixture_data/'print-done').read_text().strip()=='0'
                wait(lambda:len(stream('/printers/native/jobs'))==0)
                action('/printers/native/actions/cancel',grant,success=False)
                assert 'owner_lost' in history[-1]['stderr']
                action('/windows/'+publication['window']+'/actions/request-close')
                wait(lambda:len(stream('/accessibility/generations'))==0)
                request['last']=3
                action('/accessibility/actions/perform-action',request,success=False)
                assert 'owner_lost' in history[-1]['stderr']
            else:
                assert not stream('/accessibility/generations') and not stream('/accessibility/trees')
                assert read('/accessibility/capabilities')['remote_publication_reason']=='product_remote_semantic_service_disabled'
                action('/accessibility/actions/perform-action',success=False)
                assert 'unsupported' in history[-1]['stderr']
                action('/printers/native/actions/submit',success=False)
                assert 'unsupported' in history[-1]['stderr']
                action('/printers/native/actions/cancel',success=False)
                assert 'unsupported' in history[-1]['stderr']
                gui.key(peer,ord('w'),2)
            peer.close()
            peer=None
            stopped=subprocess.run([str(work/'stop'),str(control),'--stop'],capture_output=True,text=True,timeout=20)
            assert not stopped.returncode,stopped.stderr
            assert process.wait(timeout=25)==0
            assert not (control/'control.sock').exists()
            report=dict(status='passed',sdk_identity=json.loads((sdk/'sdk.json').read_text())['sdk_identity'],relocated_sdk=True,catalog_bindings=43,clipboard_native_generations=True,chunked_binary_transfer=True,semantic_product_enabled=semantic,semantic_native_codec_and_dispatch=semantic,prepared_print_grant_cancel=semantic,headless_native_submit_refused=semantic,volume_effects_performed=False,physical_printing=False,hardware_ports_opened=False,endpoint_cleanup=True)
            (evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
        finally:
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
