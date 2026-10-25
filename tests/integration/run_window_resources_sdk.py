#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Exercise native window owners through an independent relocated SDK client.
from pathlib import Path
import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import run_settings_sdk as gui
ROOT = Path(__file__).resolve().parents[2]


def run(args):
    """Verify live window control, native close decisions and replacement lifetimes."""
    evidence=args.evidence.resolve()
    evidence.mkdir(parents=True,exist_ok=True)
    history=[]
    with tempfile.TemporaryDirectory(prefix='stomme-windows-sdk-') as directory:
        work=Path(directory)
        sdk=work/'sdk'
        shutil.copytree(args.sdk.resolve(),sdk)
        for name,source in [('client',ROOT/'tests/integration/shell_resources_client.c'),('stop',ROOT/'examples/resource_client.c'),('fixture',ROOT/'tests/integration/window_resources_fixture.c')]:
            shutil.copy2(source,work/(name+'.c'))
        (work/'Makefile').write_text('include '+str(sdk/'sdk.mk')+'\nKITS := $(foreach kit,$(SDK_KIT_NAMES),$(SDK_ROOT)/lib/libtiku_$(kit).a)\nall: client stop fixture\n%: %.c\n\t$(CC) -Wall -Wextra -Werror -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\n')
        built=subprocess.run(['make','-j3'],cwd=work,capture_output=True,text=True)
        (evidence/'sdk-build.log').write_text(built.stdout+built.stderr)
        assert built.returncode==0,built.stderr
        home=work/'home'
        (home/'Desktop').mkdir(parents=True)
        control=work/'control'
        driver=work/'driver.sock'
        fixture_directory=work/'fixture-data'
        fixture_directory.mkdir()
        env=dict(os.environ,HOME=str(home),XDG_CONFIG_HOME=str(home/'.config'),XDG_DATA_HOME=str(home/'.local/share'),XDG_CACHE_HOME=str(home/'.cache'),STOMME_RESOURCE_DIRECTORY=str(control),STOMME_RESOURCE_TEST_PROVIDER='1',STOMME_RESOURCE_MOUNT='/windows-sdk-'+str(os.getpid()),STOMME_WINDOW_FIXTURE_DIRECTORY=str(fixture_directory))
        env.pop('DISPLAY',None)
        peer=None
        app=None
        log=(evidence/'desktop.log').open('w')
        process=subprocess.Popen([str(args.desktop.resolve()),'-conduct',str(driver)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)

        def call(operation,path,*arguments,success=True):
            """Retain each independent resource request and its outcome."""
            result=subprocess.run([str(work/'client'),str(control),operation,str(path),*map(str,arguments)],capture_output=True,text=True,timeout=20)
            history.append(dict(operation=operation,path=str(path),arguments=arguments,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
            assert (result.returncode==0)==success,history[-1]
            return result.stdout.strip()

        def read(path):
            """Read the public applied observation."""
            return json.loads(call('read',path))['applied']

        def children(path):
            """Enumerate complete child identities using the SDK cursor workflow."""
            return [line.split('\t')[0] for line in call('list',path).splitlines()]

        def find_fixture():
            """Resolve the application's current identity through its published title."""
            for path in children('/windows'):
                if read(path+'/title')['text']=='Unsaved fixture':return path
            return None

        def action(base,name,*arguments):
            """Submit a fresh-precondition operation, retrying known unapplied contention."""
            for attempt in range(8):
                result=subprocess.run([str(work/'client'),str(control),'action',base+'/actions/'+name,*map(str,arguments)],capture_output=True,text=True,timeout=20)
                history.append(dict(operation='action',path=base+'/actions/'+name,arguments=arguments,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
                if result.returncode==0:return
                assert 'stale' in result.stderr or 'busy' in result.stderr,history[-1]
                time.sleep(.08)
            raise AssertionError(history[-1])

        def fixture_state():
            """Read the application-owned receipt without interpreting desktop memory."""
            try:
                values=tuple(map(int,(fixture_directory/'state').read_text().split()))
                return values if len(values)==4 else None
            except (FileNotFoundError,ValueError):return None

        try:
            deadline=time.monotonic()+25
            while not (control/'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic()<deadline,'desktop startup failed'
                time.sleep(.02)
            peer=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer,1,struct.pack('=I',2))
            time.sleep(.3)
            app=subprocess.Popen([str(work/'fixture')],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            gui.wait_for(lambda: find_fixture() is not None,True)
            base=find_fixture()
            workspace=children('/workspaces')
            assert len(workspace)==1,workspace
            workspace=workspace[0]
            templates=[('/windows/{window}/'+name,base+'/'+name,'state') for name in ['constraints','focus','geometry','identity','owner','title','visibility','workspace']]
            templates += [('/windows/{window}/actions/'+name,base+'/actions/'+name,'action') for name in ['focus','maximize','minimize','move','request-close','resize','restore']]
            templates += [('/workspaces/{workspace}/'+name,workspace+'/'+name,'state') for name in ['active','arrangement','identity','windows']]
            audit=evidence/'coverage.tsv'
            audit.write_text(''.join(path+'\t'+kind+'\n' for template,path,kind in templates))
            assert call('audit',audit)=='19 catalog bindings verified'
            (evidence/'bindings.json').write_text(json.dumps(templates,indent=2)+'\n')
            identity=read(base+'/identity')
            owner=read(base+'/owner')
            assert owner['kind']=='application' and owner['name']=='Window fixture'
            assert read(base+'/workspace')['id']==read(workspace+'/identity')['id']
            assert identity['id'] in read(workspace+'/windows').values()
            assert read(workspace+'/active')['active'] and not read(workspace+'/arrangement')['switch_supported']
            action(base,'focus')
            gui.wait_for(lambda:gui.query(peer,'title'),'Unsaved fixture')
            action(base,'move','position',180,150)
            gui.wait_for(lambda:tuple(map(int,gui.query(peer,'frame').split()))[:2],(180,150))
            action(base,'resize','resize',500,400)
            gui.wait_for(lambda:tuple(map(int,gui.query(peer,'frame').split()))[2:],(500,400))
            gui.wait_for(lambda:fixture_state() is not None and fixture_state()[1]>0,True)
            assert fixture_state()[2]==490 and 350<=fixture_state()[3]<400,fixture_state()
            frame=read(base+'/geometry')
            gui.inject(peer,6,frame['x']+110,frame['y']+8)
            gui.inject(peer,8,frame['x']+135,frame['y']+28)
            gui.inject(peer,7,frame['x']+135,frame['y']+28)
            gui.wait_for(lambda:(read(base+'/geometry')['x'],read(base+'/geometry')['y']),(205,170))
            action(base,'maximize')
            gui.wait_for(lambda:read(base+'/geometry')['maximized'],True)
            action(base,'restore')
            gui.wait_for(lambda:read(base+'/geometry')['maximized'],False)
            action(base,'minimize')
            gui.wait_for(lambda:read(base+'/visibility')['minimized'],True)
            assert 'Unsaved fixture' in gui.query(peer,'minimized')
            action(base,'restore')
            gui.wait_for(lambda:read(base+'/visibility')['minimized'],False)
            action(base,'request-close')
            gui.wait_for(lambda:fixture_state() is not None and fixture_state()[0]>=1,True)
            assert app.poll() is None and read(base+'/visibility')['open'],'close bypassed application veto'
            (fixture_directory/'allow-close').write_text('consent\n')
            action(base,'request-close')
            assert app.wait(timeout=15)==0
            gui.wait_for(lambda:base not in children('/windows'),True)
            call('action',base+'/actions/focus',success=False)
            app=subprocess.Popen([str(work/'fixture')],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            gui.wait_for(lambda: find_fixture() is not None,True)
            replacement=find_fixture()
            assert replacement!=base and read(replacement+'/owner')['connection']!=owner['connection']
            call('action',base+'/actions/request-close',success=False)
            assert app.poll() is None and read(replacement+'/visibility')['open']
            app.kill()
            app.wait(timeout=10)
            gui.wait_for(lambda:replacement not in children('/windows'),True)
            call('action',replacement+'/actions/request-close',success=False)
            assert read(workspace+'/active')['active']
            peer.close()
            peer=None
            stopped=subprocess.run([str(work/'stop'),str(control),'--stop'],capture_output=True,text=True,timeout=15)
            assert stopped.returncode==0,stopped.stderr
            assert process.wait(timeout=20)==0
            assert not (control/'control.sock').exists()
            report=dict(status='passed',sdk_identity=json.loads((sdk/'sdk.json').read_text())['sdk_identity'],relocated_sdk=True,coverage_templates=19,native_window_control=True,gui_to_vfs_geometry=True,app_resize_receipt=True,application_close_veto=True,close_consent=True,stale_identity_rejected=True,app_disconnect_retired=True,workspace_count=1,endpoint_cleanup=True,hardware_ports_opened=False)
            (evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
        finally:
            (evidence/'history.json').write_text(json.dumps(history,indent=2)+'\n')
            if peer:peer.close()
            if app and app.poll() is None:app.terminate();app.wait(timeout=10)
            if process.poll() is None:process.terminate();process.wait(timeout=10)
            log.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--desktop',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    run(parser.parse_args())
