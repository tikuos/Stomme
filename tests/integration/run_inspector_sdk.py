#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Qualify the packaged Resource Inspector and SDK-only command client."""
from pathlib import Path
import argparse
import json
import os
import select
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time
import run_settings_sdk as gui

ROOT = Path(__file__).resolve().parents[2]


def values(text):
    """Decode consecutive bounded JSON page objects without discarding pages."""
    decoder = json.JSONDecoder()
    out = []
    while text.strip():
        text = text.lstrip()
        item, at = decoder.raw_decode(text)
        out.append(item)
        text = text[at:]
    return out


def typed(value):
    """Decode the public typed value representation printed by the command client."""
    kind = value['type']
    if kind == 'object':
        return {member['key']: typed(member['value']) for member in value.get('member', [])}
    if kind == 'array':
        return [typed(item) for item in value.get('item', [])]
    return value['value']


def run(args):
    """Exercise native controls and public clients in an isolated desktop session."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    history = []
    with tempfile.TemporaryDirectory(prefix='stomme-inspector-') as directory:
        work = Path(directory)
        sdk = work / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        shutil.copy2(ROOT / 'examples/stomme_resources.c', work / 'stomme_resources.c')
        shutil.copytree(ROOT / 'examples/resource-control', work / 'resource-control',
                        ignore=shutil.ignore_patterns('build'))
        built = subprocess.run(['make', 'SDK=' + str(sdk), 'CFLAGS=-O2 -g -Werror'],
                               cwd=work / 'resource-control', capture_output=True, text=True)
        (evidence / 'sdk-build.log').write_text(built.stdout + built.stderr)
        assert not built.returncode, built.stderr
        shutil.copy2(work / 'resource-control/build/stomme-resources', work / 'client')
        home = work / 'home'
        (home / 'Desktop').mkdir(parents=True)
        control, driver = work / 'control', work / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
                   XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'),
                   TIKU_APPS=str(args.apps.resolve()), STOMME_RESOURCE_DIRECTORY=str(control),
                   STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/inspector-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        peer = process = None
        group = None
        log = (evidence / 'desktop.log').open('w')

        def call(command, path, *arguments, success=True):
            """Run the independent public command and retain complete diagnostics."""
            result = subprocess.run([str(work / 'client'), '--directory', str(control), command,
                                     path, *map(str, arguments)], capture_output=True, text=True, timeout=20)
            history.append(dict(command=command, path=path, arguments=arguments,
                                exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
            assert (not result.returncode) == success, history[-1]
            return values(result.stdout) if result.stdout else []

        def read(path):
            """Read the applied native owner value."""
            return typed(call('read', path)[0]['applied'])

        def children(path):
            """Enumerate complete public pages, restarting unapplied stale cursors."""
            for attempt in range(20):
                try:
                    pages = call('list', path)
                    return [entry['path'] for page in pages for entry in page.get('entry', [])]
                except AssertionError:
                    if 'stale_cursor' not in history[-1]['stderr']:
                        raise
                    time.sleep(.05)
            raise AssertionError(('unstable catalog', path))

        def complete(result):
            """Require a terminal successful job without replaying the request."""
            job = result.get('job', '')
            if not job:
                return result
            end = time.monotonic() + 20
            while time.monotonic() < end:
                status = call('job', job)[0]
                if status.get('state') not in ('queued', 'running', 'awaiting-confirmation'):
                    assert status.get('code') == 'ok', status
                    return status
                time.sleep(.05)
            raise AssertionError(('job deadline', job))

        def change(command, path, *arguments):
            """Submit once and observe its initial or asynchronous native receipt."""
            return complete(call(command, path, *arguments)[0])

        def prefix(identifier):
            """Resolve an assigned application provider by its published identity."""
            for base in children('/apps'):
                if base != '/apps/catalog' and base + '/identity' in children(base):
                    identity = read(base + '/identity')
                    if identity.get('id') == identifier:
                        return base
            return None

        def wait(fn, timeout=20):
            """Wait for native publication while requiring a live desktop."""
            deadline = time.monotonic() + timeout
            last = None
            while time.monotonic() < deadline:
                last = fn()
                if last:
                    return last
                assert process.poll() is None, 'desktop exited'
                time.sleep(.08)
            raise AssertionError(('publication deadline', last, history[-3:]))

        def click(x, y):
            """Click at coordinates relative to the inspector's native content area."""
            frame = tuple(map(int, gui.query(peer, 'frame').split()))
            windows = values(subprocess.run([str(work / 'client'), '--directory', str(control),
                'stream', base + '/windows/entries'], capture_output=True, text=True, check=True).stdout)
            window = next(item for item in windows if item['title'] == 'Resource Inspector')
            history.append(dict(click=(x, y), frame=frame, window=window))
            gx, gy = frame[0] + 5 + x, frame[1] + frame[3] - window['height'] - 5 + y
            gui.inject(peer, 6, gx, gy)
            time.sleep(.1)
            gui.inject(peer, 7, gx, gy)
            time.sleep(.3)

        def replace(index, text):
            """Enter text through native editor events, including explicit select-all."""
            click(160 if index == 0 else 350, 56 if index == 0 else 403)
            gui.key(peer, ord('a'), 2)
            for char in text:
                event = gui.Event(type=4, key=ord(char), text=char.encode())
                gui.send(peer, 2, bytes(event))
                event.type = 5
                gui.send(peer, 2, bytes(event))
            time.sleep(.1)

        def inspect(path):
            """Select a path and capture its current descriptor using native controls."""
            replace(0, path)
            click(780, 58)
            wait(lambda: state()['observed_path'] == path and not state()['busy'] and not state()['refresh_required'])

        def state():
            """Read the inspector's read-only exported model observation."""
            return read(base + '/inspection')

        try:
            process = subprocess.Popen([str(args.desktop.resolve()), '-conduct', str(driver)],
                cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            group = process.pid
            wait(lambda: (control / 'control.sock').exists() and driver.exists())
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer, 1, struct.pack('=I', 2))
            time.sleep(.3)
            assert children('/')
            packaged = subprocess.run([str(args.desktop.resolve().parent / 'stomme-resources'),
                '--directory', str(control), 'read', '/about/product'], capture_output=True, text=True, timeout=15)
            (evidence / 'packaged-cli.log').write_text(packaged.stdout + packaged.stderr)
            assert not packaged.returncode, packaged.stderr
            description = call('describe', '/settings/ui/theme')[0]
            assert description['kind'] == 'setting' and description['host_control_required']
            call('set', '/settings/sound/volume', 'integer', '101', success=False)
            call('set', '/settings/ui/theme', 'json', '{"x":1,"x":2}', success=False)
            call('set', '/about/product', 'text', 'bad', success=False)
            change('invoke', '/apps/catalog/resource-inspector/actions/launch', '{}')
            base = wait(lambda: prefix('org.stomme.resource-inspector'))
            wait(lambda: state()['connected_endpoint'] == str(control) and not state()['busy'])
            gui.wait_for(lambda: gui.query(peer, 'title'), 'Resource Inspector')
            inspect('/settings/ui/theme')
            initial_pixel = gui.query(peer, x=700, y=5)
            replace(2, '{"$type":"enum","$value":"dark"}')
            click(300, 99)
            (evidence / 'after-apply.json').write_text(json.dumps(state(), indent=2))
            wait(lambda: read('/settings/ui/theme') == 'dark')
            wait(lambda: state()['request_id'] and not state()['job'] and not state()['busy'])
            assert gui.query(peer, x=700, y=5) != initial_pixel
            request_id = state()['request_id']
            call('receipt', request_id)
            inspect('/settings/ui/theme')
            watcher = subprocess.Popen([str(work / 'client'), '--directory', str(control),
                'watch', '/settings/ui/theme', '1', '10000'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            assert select.select([watcher.stdout], [], [], 5)[0], 'watch initial snapshot missing'
            change('set', '/settings/ui/theme', 'enum', 'light')
            watch_out, watch_err = watcher.communicate(timeout=15)
            (evidence / 'watch.log').write_text(watch_out + watch_err)
            assert watcher.returncode == 0 and len(values(watch_out)) == 2, watch_out + watch_err
            wait(lambda: state()['events'] > 0 and state()['refresh_required'])
            assert gui.query(peer, x=700, y=5) == initial_pixel
            change('set', '/settings/sound/volume', 'integer', '37')
            gui.wait_for(lambda: gui.query(peer, 'sounds').endswith('volume:37'), True)
            change('set', '/settings/ui/global-menu-bar', 'boolean', 'false')
            gui.wait_for(lambda: gui.query(peer, 'strip'), 'off')
            change('set', '/settings/ui/global-menu-bar', 'boolean', 'true')
            gui.wait_for(lambda: gui.query(peer, 'strip'), 'on')
            inspector_window = next(path for path in children('/windows') if read(path + '/title')['text'] == 'Resource Inspector')
            change('invoke', inspector_window + '/actions/move', '{"x":90,"y":60}')
            gui.wait_for(lambda: tuple(map(int, gui.query(peer, 'frame').split()))[:2], (90, 60))
            change('invoke', inspector_window + '/actions/focus', '{}')
            change('invoke', '/apps/catalog/calc/actions/launch', '{}')
            calc = wait(lambda: next((path for path in children('/apps') if path != '/apps/catalog' and path + '/calculation' in children(path)), None))
            calculation = read(calc + '/calculation')
            change('invoke', calc + '/commands/key', json.dumps(dict(context=calculation['context'], key='7')))
            assert read(calc + '/calculation')['display'] == '7'
            gui.wait_for(lambda: gui.query(peer, 'title'), 'Calculator')
            event = gui.Event(type=4, key=ord('8'), text=b'8')
            gui.send(peer, 2, bytes(event))
            event.type = 5
            gui.send(peer, 2, bytes(event))
            wait(lambda: read(calc + '/calculation')['display'] == '78')
            change('invoke', inspector_window + '/actions/focus', '{}')
            assert read('/services/runner/applications')
            assert read('/devices/capabilities')
            assert children('/services')
            (evidence / 'inspection.json').write_text(json.dumps(state(), indent=2) + '\n')
            change('invoke', '/test/stop', '{}')
            assert process.wait(timeout=20) == 0
            assert not (control / 'control.sock').exists()
            peer.close()
            peer = None
            prior = set(Path('/tmp').glob('stomme-desktop-*'))
            env.pop('STOMME_RESOURCE_DIRECTORY')
            driver = work / 'default-driver.sock'
            process = subprocess.Popen([str(args.desktop.resolve()), '-conduct', str(driver)],
                cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            group = process.pid
            control = wait(lambda: next((path for path in set(Path('/tmp').glob('stomme-desktop-*')) - prior if (path / 'control.sock').exists()), None))
            assert control.stat().st_mode & 0o777 == 0o700
            assert read('/about/product')
            change('invoke', '/apps/catalog/resource-inspector/actions/launch', '{}')
            base = wait(lambda: prefix('org.stomme.resource-inspector'))
            wait(lambda: state()['connected_endpoint'] == str(control) and not state()['busy'])
            change('invoke', '/test/stop', '{}')
            assert process.wait(timeout=20) == 0
            assert not control.exists(), 'automatic runtime directory retained after shutdown'
        finally:
            if peer:
                try:
                    (evidence / 'last-state.json').write_text(json.dumps(state(), indent=2))
                    (evidence / 'controls.txt').write_text(gui.query(peer, 'controls'))
                except Exception:
                    pass
                peer.close()
            if process and process.poll() is None:
                os.killpg(group, signal.SIGTERM)
                process.wait(timeout=10)
            if group:
                try:
                    os.killpg(group, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            log.close()
            (evidence / 'history.json').write_text(json.dumps(history, indent=2) + '\n')
    print('Resource Inspector and relocated SDK command workflow passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--desktop', type=Path, required=True)
    parser.add_argument('--apps', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    run(parser.parse_args())
