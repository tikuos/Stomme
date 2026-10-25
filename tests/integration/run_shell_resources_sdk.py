#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Check native Desktop settings and live observations through a relocated SDK.
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
import zlib
import run_settings_sdk as gui

ROOT = Path(__file__).resolve().parents[2]


def picture(path):
    """Write a deterministic one-pixel PNG for native image consumption."""
    def chunk(kind, data):
        """Encode one CRC-protected PNG chunk."""
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b'\0\x28\x50\x78')) + chunk(b'IEND', b''))


def run(args):
    """Exercise native settings, complete streams, GUI edits, failures and restart."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stomme-shell-sdk-') as directory:
        temp = Path(directory)
        sdk = temp / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        for name, source in [('shell', ROOT / 'tests/integration/shell_resources_client.c'), ('settings', ROOT / 'examples/settings_client.c'), ('stop', ROOT / 'examples/resource_client.c')]:
            local = temp / source.name
            shutil.copy2(source, local)
            command = ['cc', '-D_DEFAULT_SOURCE', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I' + str(sdk / 'include'), str(local), str(sdk / 'lib/libtiku_application.a'), str(sdk / 'lib/libtiku_support.a'), '-lm', '-pthread', '-o', str(temp / name)]
            result = subprocess.run(command, capture_output=True, text=True)
            (evidence / (name + '-build.log')).write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stderr
        home, desktop = temp / 'home', temp / 'Desktop'
        desktop.mkdir()
        home.mkdir()
        for index in range(240):
            (desktop / f'icon-{index:03}.txt').write_text('fixture\n')
        future = home / "Future"
        future.mkdir()
        image = temp / 'picture with spaces.png'
        picture(image)
        control, driver = temp / 'control', temp / 'driver.sock'
        env = dict(os.environ, HOME=str(home), TIKU_DESKTOP=str(desktop), XDG_CONFIG_HOME=str(home / '.config'), XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'), STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/shell-sdk-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        history = []
        process = peer = log = None

        def client(name, *values, success=True):
            """Run a source-free client and retain the request and outcome."""
            for attempt in range(8):
                result = subprocess.run([str(temp / name), str(control), *map(str, values)], capture_output=True, text=True, timeout=20)
                history.append(dict(client=name, arguments=values, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
                if values and success and result.returncode and ((values[0]=='list' and result.stderr.strip()=='stale_cursor') or (values[0]=='stream' and result.stderr.strip()=='stale_revision')):
                    time.sleep(.08)
                    continue
                assert (result.returncode == 0) == success, history[-1]
                return result.stdout.strip()
            raise AssertionError(('unstable read-only collection',values))

        def read(path):
            """Read independently reported configured, saved and effective fields."""
            return json.loads(client('shell', 'read', path))

        def record_path(family, node):
            """Resolve an opaque record identity through its public source metadata."""
            for line in client('shell', 'list', '/session/records/' + family).splitlines():
                base, kind = line.split('\t')
                assert kind == 'directory', (base, kind)
                if read(base + '/node')['desired']['node'] == str(node):
                    return base + '/value'
            raise AssertionError(('missing record', family, str(node)))

        def write_record(path, kind, *values):
            """Retry known unapplied contention with a fresh request precondition."""
            end = time.monotonic() + 12
            while True:
                result = subprocess.run([str(temp / 'shell'), str(control), 'record', path, kind, *map(str, values)], capture_output=True, text=True, timeout=20)
                history.append(dict(client='shell', arguments=['record', path, kind, *values], exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
                if not result.returncode:
                    return
                assert time.monotonic() < end and ('busy' in result.stderr or 'stale_revision' in result.stderr), history[-1]
                time.sleep(0.1)

        def start(index):
            """Start one isolated desktop and connect its native input conductor."""
            nonlocal process, peer, log
            log = (evidence / f'desktop-{index}.log').open('w')
            process = subprocess.Popen([str(args.desktop.resolve()), '-conduct', str(driver)], cwd=temp, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 25
            while not (control / 'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic() < deadline, 'desktop startup failed'
                time.sleep(0.02)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer, 1, struct.pack('=I', 2))
            time.sleep(0.5)

        def stop():
            """Stop through the explicitly enabled test action and check endpoint cleanup."""
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
            paths = ['/desktop/backdrop', '/deskbar/clock', '/deskbar/dock', '/deskbar/frame', '/session/identity', '/session/restore-policy', '/session/state']
            observations = {path: read(path) for path in paths}
            first_identity = observations['/session/identity']['applied']['incarnation']
            icons = [json.loads(row) for row in client('shell', 'stream', '/desktop/icon-layout').splitlines()]
            assert {f'icon-{index:03}.txt' for index in range(240)} <= {row['name'] for row in icons}, len(icons)
            assert all(isinstance(row['x'], int) and isinstance(row['y'], int) for row in icons)
            for path in ['/desktop/selection', '/desktop/menus', '/deskbar/teams', '/deskbar/replicants', '/deskbar/tray', '/session/startup', '/session/windows']:
                rows = [json.loads(row) for row in client('shell', 'stream', path).splitlines()]
                observations[path] = rows
            observations['/desktop/icon-layout'] = icons
            native_paths = {}
            for family, node in [('pose-info', desktop / 'icon-000.txt'), ('view-state', home), ('columns', home), ('frame', home), ('dock', '\x01session'), ('session', '\x01session')]:
                native_paths[family] = record_path(family, node)
            write_record(native_paths['pose-info'], 'pose', 160, 180, 0)
            def position():
                """Read the actual loaded icon rather than its saved record."""
                rows = [json.loads(row) for row in client('shell', 'stream', '/desktop/icon-layout').splitlines()]
                return next((row['x'], row['y']) for row in rows if row['name'] == 'icon-000.txt')
            gui.wait_for(position, (160, 180))
            write_record(native_paths['view-state'], 'view', 'list', 48)
            gui.wait_for(lambda: tuple(map(int, gui.query(peer, 'layout').split()))[:2], (0, 48))
            write_record(native_paths['columns'], 'columns', 200)
            gui.wait_for(lambda: tuple(map(int, gui.query(peer, 'layout').split()))[2:], (1, 0, 200))
            write_record(native_paths['frame'], 'frame', 100, 120, 640, 400)
            gui.wait_for(lambda: tuple(map(int, gui.query(peer, 'frame').split())), (100, 120, 640, 400))
            if args.coverage:
                coverage = client('shell', 'audit', str(args.coverage.resolve()))
            else:
                coverage = ''
            client('settings', '/settings/desktop/icon_size', 'integer', '64')
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['icon_size'], 64)
            client('shell', 'background', str(image), 2, 0, 0)
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['drawn'], True)
            assert read('/desktop/backdrop')['applied']['path'] == str(image)
            gui.wait_for(lambda: read('/settings/desktop/bg')['applied'] == read('/settings/desktop/bg')['desired'], True)
            configured = read('/settings/desktop/bg')
            assert configured['desired'] == configured['persisted'] == configured['applied'] == dict(path=str(image), mode=2, x=0, y=0)
            client('shell', 'background', str(temp / 'missing.png'), 2, 0, 0)
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['drawn'], False)
            gui.wait_for(lambda: read('/settings/desktop/bg')['applied']['path'], '')
            assert read('/settings/desktop/bg')['apply_state'] == 'fallback'
            client('shell', 'background', str(image), 2, 0, 0)
            gui.wait_for(lambda: read('/settings/desktop/bg')['applied'] == configured['applied'], True)
            gui.open_preferences(peer, 12, 'Desktop', title='')
            gui.wait_for(lambda: '64 px' in gui.query(peer, 'controls'), True)
            controls = gui.query(peer, 'controls')
            (evidence / 'desktop-controls.txt').write_text(controls)
            rectangle, flags = gui.visible_control(peer, '+', 'button')
            x, y, width, height = rectangle
            gui.click(peer, x + width // 2, y + height // 2)
            gui.wait_for(lambda: client('settings', '/settings/desktop/icon_size'), '96')
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['icon_size'], 96)
            gui.key(peer, ord('w'), 2)
            write_record(native_paths['dock'], 'dock', 4, 180)
            gui.wait_for(lambda: read('/deskbar/dock')['applied']['dock'], 4)
            write_record(native_paths['session'], 'session', str(future))
            time.sleep(0.6)
            saved_position = position()
            pose_bytes_before = os.getxattr(desktop / 'icon-000.txt', 'user._trk/pinfo').hex()
            stop()
            pose_bytes_after = os.getxattr(desktop / 'icon-000.txt', 'user._trk/pinfo').hex()
            (evidence / 'pose-persistence.json').write_text(json.dumps(dict(before=saved_position, bytes_before=pose_bytes_before, bytes_after=pose_bytes_after), indent=2) + '\n')
            start(2)
            assert read('/session/identity')['applied']['incarnation'] != first_identity
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['icon_size'], 96)
            gui.wait_for(lambda: read('/desktop/backdrop')['applied']['drawn'], True)
            assert read('/settings/desktop/bg')['persisted'] == configured['persisted']
            gui.wait_for(position, saved_position)
            gui.wait_for(lambda: gui.query(peer, 'title'), 'Future')
            assert read('/deskbar/dock')['applied']['dock'] == 4
            stop()
            report = dict(status='passed', relocated_sdk=True, native_background_consumer=True, actual_background_fallback=True, native_icon_consumer=True, native_pose_consumer=True, native_column_consumer=True, native_view_consumer=True, native_frame_consumer=True, dock_restart=True, explicit_restore_survives_shutdown=True, coverage=coverage, gui_edit=True, complete_icon_rows=len(icons), restart=True, observation_paths=len(observations), endpoint_cleanup=not (control / 'control.sock').exists(), hardware_ports_opened=False, sdk_identity=json.loads((sdk / 'sdk.json').read_text())['sdk_identity'])
            (evidence / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
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
