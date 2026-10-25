#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify Open With and Get Info against a relocated SDK and isolated desktop.
from pathlib import Path
import argparse, json, os, shutil, socket, struct, subprocess, sys, tempfile, time
from run_settings_sdk import Event, exact, send, query, inject, click, key, visible_control, wait_for
ROOT = Path(__file__).resolve().parents[2]


def menu_at_label(peer, label, xs, ys):
    """Find an observed popup label with native pointer moves."""
    for x in xs:
        for y in ys:
            inject(peer, 8, x, y)
            time.sleep(0.02)
            if query(peer, 'hot') == label:
                return x, y
    raise AssertionError(('popup label missing', label, query(peer, 'menu')))


def open_chooser(peer):
    """Open the selected file's chooser through its native context menu."""
    rectangle, flags = visible_control(peer, 'sample.txt', 'text')
    x, y, width, height = rectangle
    x, y = x + 8, y + height // 2
    click(peer, x, y)
    send(peer, 2, bytes(Event(type=6, x=x, y=y, button=3)))
    send(peer, 2, bytes(Event(type=7, x=x, y=y, button=3)))
    wait_for(lambda: 'Open with' in query(peer, 'menu'), True)
    point = menu_at_label(peer, 'Open with', [x + 18, max(20, x - 160)], range(28, 750, 6))
    click(peer, *point)
    wait_for(lambda: 'Open with…' in query(peer, 'menu'), True)
    root_x, root_y = point
    point = menu_at_label(peer, 'Open with…', [root_x + 240, root_x + 340, max(20, root_x - 240)], range(max(28, root_y - 30), 750, 6))
    click(peer, *point)
    wait_for(lambda: query(peer, 'title'), 'Open with')


def press_control(peer, label):
    """Press a rendered button by label and its current native rectangle."""
    controls = [line.split('\t') for line in query(peer, 'controls').splitlines()]
    matches = [fields for fields in controls if len(fields) == 4 and fields[0] == 'button' and fields[1].strip('"') == label]
    assert len(matches) == 1, (label, controls)
    rectangle, flags = tuple(int(n) for n in matches[0][2].split()), int(matches[0][3])
    assert not (flags & 8), (label, flags)
    x, y, width, height = rectangle
    click(peer, x + width // 2, y + height // 2)


def select_candidate(peer, name):
    """Select a visible app row without invoking its document action."""
    rectangle, flags = visible_control(peer, name, 'text')
    x, y, width, height = rectangle
    click(peer, x + 8, y + height // 2)


def run(args):
    """Exercise durable launch ordering and Info feedback through normal GUI input."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stomme-openwith-sdk-') as directory:
        temp = Path(directory)
        sdk = temp / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        for stem, source in [('settings', 'settings_client.c'), ('stop', 'resource_client.c')]:
            src = temp / source
            shutil.copy2(ROOT / 'examples' / source, src)
            command = ['cc', '-D_DEFAULT_SOURCE', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I' + str(sdk / 'include'), str(src), str(sdk / 'lib/libtiku_application.a'), str(sdk / 'lib/libtiku_support.a'), '-lm', '-pthread', '-o', str(temp / stem)]
            built = subprocess.run(command, capture_output=True, text=True)
            (evidence / (stem + '-build.log')).write_text(built.stdout + built.stderr)
            assert not built.returncode, built.stderr
        home = temp / 'home'
        config = home / '.config/tracker'
        config.mkdir(parents=True)
        (home / 'Desktop').mkdir()
        documents = temp / 'Documents'
        documents.mkdir()
        document = documents / 'sample.txt'
        document.write_text('Document preference workflow.\n')
        marker = temp / 'launch.json'
        programs = []
        for name in ['Fixture Editor', 'Fixture Viewer']:
            program = temp / name.replace(' ', '-')
            program.write_text('#!' + sys.executable + '\n' + '''from pathlib import Path
import json,os,struct,sys
records=list((Path(os.environ['XDG_DATA_HOME'])/'tracker/devices').glob('*.state'))
saved=''
if records:
 raw=records[0].read_bytes();offset=9
 while offset<len(raw):
  n,a,v=struct.unpack_from('=III',raw,offset);offset+=12;offset+=n;attr=raw[offset:offset+a];offset+=a;value=raw[offset:offset+v];offset+=v
  if attr==b'text/plain':saved=value.rstrip(b'\\0').decode()
''' + 'Path(' + repr(str(marker)) + ').write_text(json.dumps(dict(application=' + repr(name) + ',paths=sys.argv[1:],saved_handler=saved)))\n')
            program.chmod(0o700)
            programs.append(f'{name}\t{program}\ttext/plain\n')
        (config / 'apps').write_text(''.join(programs))
        (config / 'types').write_text('text/plain\tPlain text\ttext\tname,size,document\n')
        control, driver = temp / 'control', temp / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'), XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'), STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/openwith-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        path = '/settings/associations/' + b'text/plain'.hex() + '/preferred'
        history = []

        def client(*value):
            """Read or change the preferred handler through a copied SDK client."""
            result = subprocess.run([str(temp / 'settings'), str(control), path, *value], capture_output=True, text=True, timeout=20)
            history.append(dict(value=value, exit=result.returncode, out=result.stdout, err=result.stderr))
            assert not result.returncode, result.stderr
            return result.stdout.strip()

        def start(index):
            """Start the shipping desktop with an isolated folder and conductor."""
            log = open(evidence / f'desktop-{index}.log', 'w')
            process = subprocess.Popen([str(args.desktop.resolve()), str(documents), '-conduct', str(driver)], cwd=temp, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 20
            while not control.joinpath('control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic() < deadline, 'desktop startup failed'
                time.sleep(0.02)
            time.sleep(0.3)
            return process, log

        def stop(process):
            """Request orderly shutdown through the explicitly enabled test owner."""
            result = subprocess.run([str(temp / 'stop'), str(control), '--stop'], capture_output=True, text=True, timeout=15)
            assert not result.returncode and process.wait(timeout=20) == 0, result.stderr

        process = log = peer = None
        try:
            process, log = start(1)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            send(peer, 1, struct.pack('=I', 2))
            wait_for(lambda: 'sample.txt' in query(peer, 'controls'), True)
            assert client('text', 'Fixture Editor') == 'Fixture Editor'
            open_chooser(peer)
            select_candidate(peer, 'Fixture Viewer')
            (evidence / 'chooser-selected.txt').write_text(query(peer, 'controls') + '\nMENU:' + query(peer, 'menu') + '\n' + query(peer, 'narrate'))
            sidecars = list((home / '.local/share/tracker/devices').glob('*.state'))
            assert len(sidecars) == 1, sidecars
            sidecar = sidecars[0]
            backup = sidecar.with_suffix('.saved')
            sidecar.rename(backup)
            sidecar.mkdir()
            press_control(peer, 'Open and make preferred')
            wait_for(lambda: 'Could not save preferred app' in query(peer, 'controls'), True)
            assert query(peer, 'title') == 'Open with' and not marker.exists()
            assert client() == 'Fixture Editor'
            sidecar.rmdir()
            backup.rename(sidecar)
            press_control(peer, 'Open and make preferred')
            wait_for(marker.exists, True)
            launch = json.loads(marker.read_text())
            (evidence / 'preferred-launch.json').write_text(json.dumps(launch, indent=2) + '\n')
            assert launch == dict(application='Fixture Viewer', paths=[str(document)], saved_handler='Fixture Viewer'), launch
            assert client() == 'Fixture Viewer'
            wait_for(lambda: query(peer, 'title') == 'Open with', False)
            marker.unlink()
            open_chooser(peer)
            select_candidate(peer, 'Fixture Editor')
            press_control(peer, 'Open')
            wait_for(marker.exists, True)
            normal = json.loads(marker.read_text())
            (evidence / 'normal-launch.json').write_text(json.dumps(normal, indent=2) + '\n')
            assert normal == dict(application='Fixture Editor', paths=[str(document)], saved_handler='Fixture Viewer'), normal
            assert client() == 'Fixture Viewer'
            marker.unlink()
            rectangle, flags = visible_control(peer, 'sample.txt', 'text')
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            key(peer, ord('i'), 2)
            wait_for(lambda: 'Fixture Viewer' in query(peer, 'controls'), True)
            assert client('text', 'Fixture Editor') == 'Fixture Editor'
            wait_for(lambda: 'Fixture Editor' in query(peer, 'controls'), True)
            rectangle, flags = visible_control(peer, 'Fixture Editor', 'text')
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            wait_for(lambda: client(), 'Fixture Viewer')
            wait_for(lambda: 'Fixture Viewer' in query(peer, 'controls'), True)
            sidecar.rename(backup)
            sidecar.mkdir()
            rectangle, flags = visible_control(peer, 'Fixture Viewer', 'text')
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            wait_for(lambda: 'Could not save the preferred application' in query(peer, 'status'), True)
            assert client() == 'Fixture Viewer' and not marker.exists()
            sidecar.rmdir()
            backup.rename(sidecar)
            peer.close()
            peer = None
            stop(process)
            log.close()
            log = None
            process, log = start(2)
            assert client() == 'Fixture Viewer'
            stop(process)
            log.close()
            log = None
            assert not (control / 'control.sock').exists()
            report = dict(status='passed', sdk_identity=json.loads((sdk / 'sdk.json').read_text())['sdk_identity'], independent_relocated_sdk=True, source_includes=False, openwith_durable_before_launch=True, openwith_save_failure=True, openwith_retry=True, normal_open_preserves_preference=True, info_gui_to_api=True, info_api_to_gui=True, info_failed_save=True, restart=True, owned_endpoint_cleanup=True, hardware_ports_opened=False)
            (evidence / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(report, indent=2))
        finally:
            if peer:
                try:
                    (evidence / 'last-gui.txt').write_text(query(peer, 'title') + '\n' + query(peer, 'controls') + '\nMENU:' + query(peer, 'menu') + '\nSTATUS:' + query(peer, 'status'))
                except (OSError, AssertionError):
                    pass
            (evidence / 'client-history.json').write_text(json.dumps(history, indent=2) + '\n')
            if peer:
                peer.close()
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if log:
                log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--desktop', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    run(parser.parse_args())
