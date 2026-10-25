#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify shared settings through SDK clients, rendered GUI state and persistence.
from pathlib import Path
import argparse, ctypes, json, os, shutil, socket, struct, subprocess, tempfile, time
ROOT = Path(__file__).resolve().parents[2]

class Event(ctypes.Structure):
    """Native conductor input record matching the exported SDK event layout."""
    _fields_ = [('type', ctypes.c_int), ('time_us', ctypes.c_int64), ('modifiers', ctypes.c_uint), ('key', ctypes.c_uint), ('button', ctypes.c_uint), ('repeat', ctypes.c_uint), ('text', ctypes.c_char * 8), ('x', ctypes.c_int), ('y', ctypes.c_int), ('dx', ctypes.c_int), ('dy', ctypes.c_int), ('width', ctypes.c_int), ('height', ctypes.c_int)]

def exact(peer, n):
    """Read a complete conductor frame segment or reject a closed stream."""
    data = b''
    while len(data) < n:
        more = peer.recv(n - len(data))
        assert more, 'conductor closed'
        data += more
    return data

def send(peer, kind, data):
    """Send one native conductor protocol frame."""
    peer.sendall(struct.pack('=II', kind, len(data)) + data)

def query(peer, name=None, x=0, y=0):
    """Read a rendered pixel or named desktop fact through conductor version 2."""
    send(peer, 3, struct.pack('=Iii64s', 2 if name else 1, x, y, (name or '').encode()))
    while True:
        kind, size = struct.unpack('=II', exact(peer, 8))
        payload = exact(peer, size)
        if kind == 16:
            ok, num = struct.unpack('=ii', payload[:8])
            assert ok, (name, x, y)
            return payload[8:].split(b'\x00')[0].decode() if name else num

def inject(peer, kind, x=0, y=0, key=0, mods=0):
    """Inject a native input event through the existing test conductor."""
    event = Event(type=kind, x=x, y=y, key=key, modifiers=mods, button=1 if kind in (6, 7) else 0)
    send(peer, 2, bytes(event))

def click(peer, x, y):
    """Press and release one GUI control."""
    inject(peer, 6, x, y)
    inject(peer, 7, x, y)
    time.sleep(0.08)

def key(peer, code, modifiers=0):
    """Press and release a native logical key."""
    inject(peer, 4, key=code, mods=modifiers)
    inject(peer, 5, key=code, mods=modifiers)
    time.sleep(0.08)


def open_preferences(peer, size, wanted="Fonts", title="Fonts"):
    """Open a preferences window through observed Deskbar menu labels."""
    key(peer, ord('w'), 2)
    click(peer, 500, 650)
    click(peer, 980, 40)
    labels = query(peer, 'menu').split('|')[:-1]
    assert 'Settings' in labels, (labels, query(peer, 'title'), query(peer, 'menu-bottom'))
    root_count = len(labels)
    index = labels.index('Settings')
    row = 21 + size - 12
    bottom = int(query(peer, 'menu-bottom'))
    y = bottom - len(labels) * row - 4 + 2 + index * row + row // 2
    inject(peer, 8, 800, y)
    wait_for(lambda: query(peer, 'hot'), 'Settings')
    click(peer, 800, y)
    labels = query(peer, 'menu').split('|')[:-1][root_count:]
    index = next(i for i, label in enumerate(labels) if wanted in label)
    target = y - row // 2 + 2 + index * row + row // 2
    inject(peer, 8, 600, target)
    wait_for(lambda: query(peer, 'hot'), labels[index])
    click(peer, 600, target)
    if title:
        wait_for(lambda: query(peer, 'title'), title)
    else:
        wait_for(lambda: 'preferences' in query(peer, 'title').lower(), True)
    return bottom


def visible_control(peer, label, kind=None):
    """Read one visible control's rectangle and flags from the rendered window."""
    for line in query(peer, 'controls').splitlines():
        fields = line.split('\t')
        if len(fields) == 4 and (kind is None or fields[0] == kind) and fields[1].strip('"').startswith(label):
            return tuple(int(number) for number in fields[2].split()), int(fields[3])
    raise AssertionError(('control missing', label, query(peer, 'controls')))


def association_radios(peer):
    """Read the association window's recorded radio rectangles and checked flags."""
    rows = []
    for line in query(peer, 'controls').splitlines():
        fields = line.split('\t')
        if len(fields) == 4 and fields[0] == 'radio':
            rows.append((tuple(int(n) for n in fields[2].split()), int(fields[3])))
    assert rows, query(peer, 'controls')
    return rows


def open_association(peer, label):
    """Select a type by its rendered label while paging the native list."""
    open_preferences(peer, 12, 'File Associations', 'File Associations')
    for page in range(20):
        controls = query(peer, 'controls')
        for line in controls.splitlines():
            fields = line.split('\t')
            if len(fields) == 4 and fields[0] == 'text' and fields[1].strip('"') == label:
                x, y, width, height = (int(n) for n in fields[2].split())
                click(peer, x + 8, y + height // 2)
                return
        next_buttons = []
        for line in controls.splitlines():
            fields = line.split('\t')
            if len(fields) == 4 and fields[0] == 'button' and fields[1].strip('"') == '>':
                next_buttons.append(tuple(int(n) for n in fields[2].split()))
        assert next_buttons, controls
        x, y, width, height = min(next_buttons)
        click(peer, x + width // 2, y + height // 2)
    raise AssertionError(('association type not visible', label, query(peer, 'controls')))


def wait_for(fn, expected, timeout=12):
    """Poll a condition until it matches or its deadline expires."""
    end = time.monotonic() + timeout
    last = None
    while time.monotonic() < end:
        last = fn()
        if last == expected:
            return last
        time.sleep(0.05)
    raise AssertionError(('waiting for', expected, 'last', last))

def run(args):
    """Qualify an isolated desktop using relocated SDK clients and normal GUI input."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sv3-runtime-') as directory:
        temp = Path(directory)
        sdk = temp / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        for stem, source in [('settings', 'settings_client.c'), ('stop', 'resource_client.c'), ('profile', 'settings_profile_client.c')]:
            src = temp / source
            shutil.copy2(ROOT / ('tests/integration' if stem == 'profile' else 'examples') / source, src)
            cmd = ['cc', '-D_DEFAULT_SOURCE', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I' + str(sdk / 'include'), str(src), str(sdk / 'lib/libtiku_application.a'), str(sdk / 'lib/libtiku_support.a'), '-lm', '-pthread', '-o', str(temp / stem)]
            built = subprocess.run(cmd, capture_output=True, text=True)
            (evidence / (stem + '-build.log')).write_text(built.stdout + built.stderr)
            assert not built.returncode, built.stderr
        home = temp / 'home'
        (home / 'Desktop').mkdir(parents=True)
        catalog_dir = home / '.config/tracker'
        catalog_dir.mkdir(parents=True, exist_ok=True)
        (catalog_dir / 'apps').write_text('Fixture Editor\t/bin/true\ttext/plain\nFixture Viewer\t/bin/false\ttext/plain\n')
        types_file = catalog_dir / 'types'
        types_file.write_text('text/plain\tRuntime type\ttext\tname,size,document\n')
        association_path = '/settings/associations/' + b'text/plain'.hex() + '/preferred'
        type_path = '/settings/file-types/' + b'text/plain'.hex()
        control = temp / 'control'
        driver = temp / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'), XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'), STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/sv3-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        history = []

        def client(path, *value):
            """Run the independent settings client and retain its result."""
            result = subprocess.run([str(temp / 'settings'), str(control), path, *value], capture_output=True, text=True, timeout=20)
            history.append(dict(path=path, value=value, exit=result.returncode, out=result.stdout, err=result.stderr))
            return result

        def read(path):
            """Read one typed preference value."""
            result = client(path)
            assert result.returncode == 0, result.stderr
            return result.stdout.strip()

        def set_value(path, *value):
            """Submit one durable typed change and require success."""
            result = client(path, *value)
            assert result.returncode == 0, result.stderr
            return result.stdout.strip()

        def stop(process):
            """Stop the explicitly enabled test owner and require endpoint shutdown."""
            result = subprocess.run([str(temp / 'stop'), str(control), '--stop'], capture_output=True, text=True, timeout=15)
            assert not result.returncode, result.stderr
            assert process.wait(timeout=20) == 0

        def start(index):
            """Launch one desktop session with an isolated home and test endpoint."""
            log = open(evidence / f'desktop-{index}.log', 'w')
            process = subprocess.Popen([str(args.desktop.resolve()), '-conduct', str(driver)], cwd=temp, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 20
            while not control.joinpath('control.sock').exists() or not driver.exists():
                assert process.poll() is None, 'desktop startup failed'
                assert time.monotonic() < deadline, 'desktop startup timeout'
                time.sleep(0.02)
            time.sleep(0.3)
            return (process, log)
        process = None
        peer = None
        log = None
        try:
            process, log = start(1)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            send(peer, 1, struct.pack('=I', 2))
            initial = query(peer, x=700, y=5)
            assert read('/settings/ui/theme') == 'light'
            assert set_value('/settings/ui/theme', 'enum', 'dark') == 'dark'
            wait_for(lambda: query(peer, x=700, y=5) == initial, False)
            dark = query(peer, x=700, y=5)
            inject(peer, 4, key=ord(','), mods=2)
            inject(peer, 5, key=ord(','), mods=2)
            time.sleep(0.2)
            (evidence / 'prefs-title.txt').write_text(query(peer, 'title'))
            (evidence / 'prefs-controls.txt').write_text(query(peer, 'controls'))
            click(peer, 315, 205)
            wait_for(lambda: 'DarkMode' in query(peer, 'controls'), True)
            click(peer, 435, 357)
            wait_for(lambda: read('/settings/ui/theme'), 'monochrome')
            assert set_value('/settings/ui/global-menu-bar', 'boolean', 'false') == 'false'
            wait_for(lambda: query(peer, 'strip'), 'off')
            assert set_value('/settings/ui/global-menu-bar', 'boolean', 'true') == 'true'
            wait_for(lambda: query(peer, 'strip'), 'on')
            assert set_value('/settings/sound/volume', 'integer', '37') == '37'
            wait_for(lambda: query(peer, 'sounds').endswith('volume:37'), True)
            assert set_value('/settings/sound/beep/enabled', 'boolean', 'false') == 'false'
            wait_for(lambda: 'beep:' in query(peer, 'sounds') and ':off' in query(peer, 'sounds').split('beep:')[1].split(' ')[0], True)
            profile = subprocess.run([str(temp / 'profile'), str(control)], capture_output=True, text=True, timeout=25)
            (evidence / 'profile.log').write_text(profile.stdout + profile.stderr)
            assert profile.returncode == 0, profile.stderr
            assert read('/settings/sound/volume') == '42'
            wait_for(lambda: query(peer, 'sounds').endswith('volume:42'), True)
            settings = list((home / '.config').rglob('*TrackerSettings*'))
            assert len(settings) == 1, settings
            file = settings[0]
            backup = file.with_name('TrackerSettings.backup')
            file.rename(backup)
            file.mkdir()
            failed = client('/settings/ui/theme', 'enum', 'light')
            assert failed.returncode != 0
            assert read('/settings/ui/theme') == 'monochrome'
            file.rmdir()
            backup.rename(file)
            prior = file.stat()
            text = file.read_text()
            assert 'DarkMode 2' in text, text
            file.write_text(text.replace('DarkMode 2', 'DarkMode 0'))
            os.utime(file, ns=(prior.st_atime_ns, prior.st_mtime_ns))
            assert file.stat().st_size == prior.st_size
            wait_for(lambda: read('/settings/ui/theme'), 'light')
            wait_for(lambda: query(peer, x=700, y=5), initial)
            assert set_value('/settings/ui/font/size', 'integer', '14') == '14'
            assert read('/settings/deskbar/font_size') == '14'
            menu_at_14 = open_preferences(peer, 14)
            key(peer, 13)
            key(peer, 13)
            key(peer, 12)
            wait_for(lambda: read('/settings/ui/font/size'), '16')
            assert read('/settings/deskbar/font_size') == '16'
            key(peer, ord('w'), 2)
            click(peer, 980, 40)
            menu_at_16 = int(query(peer, 'menu-bottom'))
            assert menu_at_16 > menu_at_14, (menu_at_14, menu_at_16)
            key(peer, 11)
            assert set_value('/settings/ui/font/size', 'integer', '12') == '12'
            open_preferences(peer, 12, 'Desktop preferences', None)
            assert set_value('/settings/ui/reduce-motion', 'boolean', 'true') == 'true'
            wait_for(lambda: bool(visible_control(peer, 'Reduce motion')[1] & 16), True)
            rectangle, flags = visible_control(peer, 'Reduce motion')
            x, y, width, height = rectangle
            click(peer, x + width // 2, y + height // 2)
            wait_for(lambda: read('/settings/ui/reduce-motion'), 'false')
            wait_for(lambda: bool(visible_control(peer, 'Reduce motion')[1] & 16), False)
            motion_backup = file.with_name('TrackerSettings.motion-saved')
            file.rename(motion_backup)
            file.mkdir()
            click(peer, x + width // 2, y + height // 2)
            time.sleep(0.2)
            assert read('/settings/ui/reduce-motion') == 'false'
            assert not (visible_control(peer, 'Reduce motion')[1] & 16)
            file.rmdir()
            motion_backup.rename(file)
            key(peer, ord('w'), 2)
            sidecars = list((home / '.local/share/tracker/devices').glob('*.state'))
            assert len(sidecars) == 1, sidecars
            sidecar = sidecars[0]
            saved_sidecar = sidecar.with_suffix('.saved')
            sidecar.rename(saved_sidecar)
            sidecar.mkdir()
            failed_record = client('/settings/deskbar/show_clock', 'boolean', 'false')
            assert failed_record.returncode != 0
            assert read('/settings/deskbar/show_clock') == 'true'
            sidecar.rmdir()
            saved_sidecar.rename(sidecar)
            assert set_value('/settings/deskbar/show_clock', 'boolean', 'false') == 'false'
            assert set_value('/settings/deskbar/show_clock', 'boolean', 'true') == 'true'
            raw = bytearray(sidecar.read_bytes())
            assert raw.startswith(b'TRKSTATE1')
            offset = 9
            updated = False
            while offset < len(raw):
                node_size, attr_size, value_size = struct.unpack_from('=III', raw, offset)
                offset += 12
                node = raw[offset:offset + node_size]
                offset += node_size
                attr = raw[offset:offset + attr_size]
                offset += attr_size
                if node == b'\x01session' and attr == b'_trk/deskbar_prefs':
                    assert value_size == 92
                    struct.pack_into('=i', raw, offset + 6 * 4, 48)
                    updated = True
                offset += value_size
            assert updated
            previous_stat = sidecar.stat()
            sidecar.write_bytes(raw)
            os.utime(sidecar, ns=(previous_stat.st_atime_ns, previous_stat.st_mtime_ns))
            assert sidecar.stat().st_size == previous_stat.st_size
            wait_for(lambda: read('/settings/deskbar/icon_size'), '48')
            details = json.loads(read(association_path.replace('/preferred', '/details')))
            assert details['mime'] == 'text/plain' and {'Fixture Editor', 'Fixture Viewer'} <= set(details['candidates'])
            editor_radio = details['candidates'].index('Fixture Editor') + 1
            viewer_radio = details['candidates'].index('Fixture Viewer') + 1
            assert json.loads(read(type_path))['description'] == 'Runtime type'
            assert set_value(association_path, 'text', 'Fixture Viewer') == 'Fixture Viewer'
            open_association(peer, details['label'])
            wait_for(lambda: bool(association_radios(peer)[viewer_radio][1] & 16), True)
            rectangle, flags = association_radios(peer)[editor_radio]
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            rectangle, flags = visible_control(peer, 'Save', 'button')
            x, y, width, height = rectangle
            click(peer, x + width // 2, y + height // 2)
            wait_for(lambda: read(association_path), 'Fixture Editor')
            wait_for(lambda: query(peer, 'title') == 'File Associations', False)
            open_association(peer, details['label'])
            assert set_value(association_path, 'text', 'Fixture Viewer') == 'Fixture Viewer'
            wait_for(lambda: bool(association_radios(peer)[viewer_radio][1] & 16), True)
            rectangle, flags = association_radios(peer)[editor_radio]
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            assert set_value(association_path, 'text', '') == ''
            rectangle, flags = visible_control(peer, 'Save', 'button')
            x, y, width, height = rectangle
            click(peer, x + width // 2, y + height // 2)
            wait_for(lambda: 'Associations changed' in query(peer, 'controls'), True)
            assert read(association_path) == ''
            key(peer, ord('w'), 2)
            open_association(peer, details['label'])
            rectangle, flags = association_radios(peer)[viewer_radio]
            x, y, width, height = rectangle
            click(peer, x + 8, y + height // 2)
            sidecar.rename(saved_sidecar)
            sidecar.mkdir()
            rectangle, flags = visible_control(peer, 'Save', 'button')
            x, y, width, height = rectangle
            click(peer, x + width // 2, y + height // 2)
            wait_for(lambda: 'Could not save' in query(peer, 'controls'), True)
            assert read(association_path) == ''
            sidecar.rmdir()
            saved_sidecar.rename(sidecar)
            click(peer, x + width // 2, y + height // 2)
            wait_for(lambda: read(association_path), 'Fixture Viewer')
            before_type = types_file.stat()
            types_file.write_text(types_file.read_text().replace('Runtime type', 'Changed type'))
            os.utime(types_file, ns=(before_type.st_atime_ns, before_type.st_mtime_ns))
            assert types_file.stat().st_size == before_type.st_size
            wait_for(lambda: json.loads(read(type_path))['description'], 'Changed type')
            peer.close()
            peer = None
            stop(process)
            log.close()
            log = None
            process, log = start(2)
            assert read('/settings/ui/theme') == 'light'
            assert read('/settings/deskbar/icon_size') == '48'
            assert read('/settings/ui/font/size') == '12'
            assert read(association_path) == 'Fixture Viewer'
            assert json.loads(read(type_path))['description'] == 'Changed type'
            stop(process)
            log.close()
            log = None
            assert not (control / 'control.sock').exists()
            report = dict(status='passed', sdk_identity=json.loads((sdk / 'sdk.json').read_text())['sdk_identity'], independent_relocated_sdk=True, source_includes=False, api_to_rendered_theme=True, gui_to_api_theme=True, api_to_menu_bar=True, api_to_sound_settings=True, composite_profile_ipc=True, api_to_font_metrics=True, gui_to_font_api=True, reduce_motion_gui_sync=True, record_save_failure=True, same_stat_record_reload=True, record_restart=True, association_gui_api=True, association_stale_draft=True, association_failed_save=True, association_restart=True, loaded_type_catalog=True, type_edit_requires_restart=True, failed_save_preserves_model=True, same_size_same_timestamp_external_reload=True, restart=True, owned_endpoint_cleanup=True, initial_pixel=initial, dark_pixel=dark, hardware_ports_opened=False)
            (evidence / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(report, indent=2))
        finally:
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
