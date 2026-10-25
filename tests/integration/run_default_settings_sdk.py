#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify folder defaults and preference save feedback through copied SDK clients.
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
    """Check native consumers, failed GUI saves, retry, catalog coverage and restart."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stomme-default-sdk-') as directory:
        temp = Path(directory)
        sdk = temp / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        for name, source in [('defaults', ROOT / 'tests/integration/default_settings_client.c'), ('settings', ROOT / 'examples/settings_client.c'), ('stop', ROOT / 'examples/resource_client.c')]:
            local = temp / source.name
            shutil.copy2(source, local)
            command = ['cc', '-D_DEFAULT_SOURCE', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I' + str(sdk / 'include'), str(local), str(sdk / 'lib/libtiku_application.a'), str(sdk / 'lib/libtiku_support.a'), '-lm', '-pthread', '-o', str(temp / name)]
            result = subprocess.run(command, capture_output=True, text=True)
            (evidence / (name + '-build.log')).write_text(result.stdout + result.stderr)
            assert not result.returncode, result.stderr
        home = temp / 'home'
        (home / 'Desktop').mkdir(parents=True)
        documents = home / 'Documents'
        for name in ['A', 'B', 'C']:
            (documents / name).mkdir(parents=True)
        control, driver = temp / 'control', temp / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'), XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'), STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/default-sdk-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        history, frames = [], []
        process = peer = log = None

        def client(name, *values, success=True):
            """Run a source-free client and retain its arguments and outcome."""
            result = subprocess.run([str(temp / name), str(control), *map(str, values)], capture_output=True, text=True, timeout=20)
            history.append(dict(client=name, arguments=values, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
            assert (result.returncode == 0) == success, history[-1]
            return result.stdout.strip()

        def read():
            """Read configured, saved and effective default policy values."""
            return json.loads(client('defaults', 'read'))

        def geometry():
            """Read the focused native window's observed rectangle."""
            return tuple(int(n) for n in gui.query(peer, 'frame').split())

        def open_folder(name):
            """Select a folder by its rendered label and open it with the native shortcut."""
            matches = [line.split('\t') for line in gui.query(peer, 'controls').splitlines() if len(line.split('\t')) == 4 and line.split('\t')[0] == 'text' and line.split('\t')[1].strip(chr(34)) == name]
            assert len(matches) == 1, (name, gui.query(peer, 'controls'))
            x, y, width, height = (int(n) for n in matches[0][2].split())
            gui.click(peer, x + 8, y + height // 2)
            gui.key(peer, 2, 2)
            gui.wait_for(lambda: gui.query(peer, 'title') == name, True)
            frames.append(dict(folder=name, rectangle=geometry()))
            return geometry()

        def close_folder():
            """Close a child while keeping its parent window available."""
            gui.key(peer, ord('w'), 2)
            gui.wait_for(lambda: gui.query(peer, 'title') == 'Documents', True)

        def start(index):
            """Launch an isolated shipping desktop and connect its conductor."""
            nonlocal process, peer, log
            log = (evidence / f'desktop-{index}.log').open('w')
            process = subprocess.Popen([str(args.desktop.resolve()), str(documents), '-conduct', str(driver)], cwd=temp, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 20
            while not (control / 'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic() < deadline, 'desktop startup failed'
                time.sleep(0.02)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer, 1, struct.pack('=I', 2))
            gui.wait_for(lambda: 'A' in gui.query(peer, 'controls'), True)
            time.sleep(0.3)

        def stop():
            """Shut down through the explicitly enabled fixture action."""
            nonlocal process, peer, log
            peer.close()
            peer = None
            client('stop', '--stop')
            assert process.wait(timeout=20) == 0
            process = None
            log.close()
            log = None

        try:
            start(1)
            original = geometry()
            assert read()['desired'] == {}
            client('settings', '/settings/tracker/singlewindowbrowse', 'boolean', 'false')
            client('defaults', 'set', 120, 100, 580, 360)
            saved = read()
            assert saved['desired'] == saved['persisted'] == saved['applied'] == dict(x=120, y=100, width=580, height=360)
            assert geometry() == original
            assert open_folder('A') == (120, 100, 580, 360)
            close_folder()
            client('defaults', 'set', 200, 140, 640, 400)
            assert open_folder('B') == (200, 140, 640, 400)
            close_folder()
            assert open_folder('A') == (120, 100, 580, 360)
            close_folder()
            client('defaults', 'set', 0, 0, 179, 400, success=False)
            assert read()['persisted']['width'] == 640
            sidecars = list((home / '.local/share/tracker/devices').glob('*.state'))
            assert len(sidecars) == 1
            sidecar = sidecars[0]
            backup = sidecar.with_suffix('.saved')
            sidecar.rename(backup)
            sidecar.mkdir()
            client('defaults', 'set', 300, 200, 660, 440, success=False)
            assert read()['persisted']['width'] == 640
            sidecar.rmdir()
            backup.rename(sidecar)
            client('defaults', 'set', 200, 140, 640, 400)
            gui.open_preferences(peer, 12, 'Fonts', 'Fonts')
            assert 'Could not save settings' not in gui.query(peer, 'controls')
            before_size = client('settings', '/settings/ui/font/size')
            sidecar.rename(backup)
            sidecar.mkdir()
            rectangle, flags = gui.visible_control(peer, '+', 'button')
            x, y, width, height = rectangle
            gui.click(peer, x + width // 2, y + height // 2)
            gui.wait_for(lambda: 'Could not save settings' in gui.query(peer, 'controls'), True)
            assert client('settings', '/settings/ui/font/size') == before_size
            time.sleep(0.7)
            assert 'Could not save settings' in gui.query(peer, 'controls')
            (evidence / 'fonts-failed-save-controls.txt').write_text(gui.query(peer, 'controls'))
            sidecar.rmdir()
            backup.rename(sidecar)
            rectangle, flags = gui.visible_control(peer, '+', 'button')
            x, y, width, height = rectangle
            gui.click(peer, x + width // 2, y + height // 2)
            gui.wait_for(lambda: client('settings', '/settings/ui/font/size'), '14')
            gui.wait_for(lambda: 'Could not save settings' in gui.query(peer, 'controls'), False)
            gui.open_preferences(peer, 14, 'Deskbar preferences', 'Deskbar preferences')
            before_sort = client('settings', '/settings/deskbar/sort_apps')
            sidecar.rename(backup)
            sidecar.mkdir()
            rectangle, flags = gui.visible_control(peer, 'Sort applications by name', 'text')
            x, y, width, height = rectangle
            gui.click(peer, x + 8, y + height // 2)
            gui.wait_for(lambda: 'Could not save settings' in gui.query(peer, 'controls'), True)
            assert client('settings', '/settings/deskbar/sort_apps') == before_sort
            (evidence / 'deskbar-failed-save-controls.txt').write_text(gui.query(peer, 'controls'))
            sidecar.rmdir()
            backup.rename(sidecar)
            rectangle, flags = gui.visible_control(peer, 'Sort applications by name', 'text')
            x, y, width, height = rectangle
            gui.click(peer, x + 8, y + height // 2)
            gui.wait_for(lambda: client('settings', '/settings/deskbar/sort_apps'), 'false' if before_sort == 'true' else 'true')
            gui.wait_for(lambda: 'Could not save settings' in gui.query(peer, 'controls'), False)
            coverage = client('defaults', 'audit', str(args.coverage.resolve())) if args.coverage else ''
            gui.key(peer, ord('w'), 2)
            stop()
            start(2)
            assert read()['desired'] == read()['persisted'] == dict(x=200, y=140, width=640, height=400)
            assert geometry() == original
            assert client('settings', '/settings/ui/font/size') == '14'
            assert open_folder('A') == (120, 100, 580, 360)
            close_folder()
            parent = geometry()
            assert open_folder('C') == (parent[0] + 22, parent[1] + 22, parent[2], parent[3])
            close_folder()
            client('defaults', 'clear')
            assert read()['desired'] == read()['persisted'] == read()['applied'] == {}
            stop()
            report = dict(status='passed', sdk_identity=json.loads((sdk / 'sdk.json').read_text())['sdk_identity'], independent_relocated_sdk=True, source_includes=False, native_default_consumer=True, existing_windows_unchanged=True, own_frame_precedence=True, parent_frame_precedence=True, default_clear=True, invalid_bounds=True, failed_default_save=True, fonts_gui_failure_feedback=True, fonts_gui_retry=True, deskbar_gui_failure_feedback=True, deskbar_gui_retry=True, error_survives_reload=True, restart=True, coverage=coverage, frames=frames, owned_endpoint_cleanup=not (control / 'control.sock').exists(), hardware_ports_opened=False)
            (evidence / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(report, indent=2))
        finally:
            (evidence / 'history.json').write_text(json.dumps(history, indent=2) + '\n')
            if peer:
                try:
                    (evidence / 'last-gui.txt').write_text(gui.query(peer, 'title') + '\n' + gui.query(peer, 'controls'))
                except (OSError, AssertionError):
                    pass
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
    parser.add_argument('--coverage', type=Path)
    run(parser.parse_args())
