#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Qualify native file-definition edits through a relocated SDK and shipping desktop.
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
    """Exercise durable edits, active records, reload, failure and desktop restart."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stomme-type-sdk-') as directory:
        temp = Path(directory)
        sdk = temp / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        for name, source in [('types', ROOT / 'tests/integration/type_settings_client.c'), ('stop', ROOT / 'examples/resource_client.c')]:
            local = temp / source.name
            shutil.copy2(source, local)
            command = ['cc', '-D_DEFAULT_SOURCE', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I' + str(sdk / 'include'), str(local), str(sdk / 'lib/libtiku_application.a'), str(sdk / 'lib/libtiku_support.a'), '-lm', '-pthread', '-o', str(temp / name)]
            result = subprocess.run(command, capture_output=True, text=True)
            (evidence / (name + '-build.log')).write_text(result.stdout + result.stderr)
            assert not result.returncode, result.stderr
        home = temp / 'home'
        (home / 'Desktop').mkdir(parents=True)
        config = home / '.config/tracker'
        config.mkdir(parents=True)
        (config / 'apps').write_text('Fixture Reader\t/bin/true\tapplication/x-native-fixture\n')
        types = config / 'types'
        types.write_text('# retained comment\nopaque extension line\napplication/x-native-fixture\tLoaded text\tfile\tsize,document,future:column\n')
        control, driver = temp / 'control', temp / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'), XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'), STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1', STOMME_RESOURCE_MOUNT='/type-sdk-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        history = []
        process = peer = log = None

        def client(mode, mime='application/x-native-fixture', description=None, success=True):
            """Run the independent SDK client and record its complete result."""
            command = [str(temp / 'types'), str(control), mode, mime]
            if description is not None:
                command.append(description)
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            history.append(dict(mode=mode, mime=mime, description=description, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
            assert (result.returncode == 0) == success, history[-1]
            return json.loads(result.stdout) if mode == 'read' and success else result

        def start(index):
            """Launch the product with isolated configuration and conductor input."""
            nonlocal process, peer, log
            log = (evidence / f'desktop-{index}.log').open('w')
            process = subprocess.Popen([str(args.desktop.resolve()), '-conduct', str(driver)], cwd=temp, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 20
            while not (control / 'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic() < deadline, 'desktop startup failed'
                time.sleep(0.02)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer, 1, struct.pack('=I', 2))
            time.sleep(0.5)

        def stop():
            """Stop the explicitly enabled fixture owner and await desktop exit."""
            nonlocal process, peer, log
            peer.close()
            peer = None
            result = subprocess.run([str(temp / 'stop'), str(control), '--stop'], capture_output=True, text=True, timeout=15)
            assert not result.returncode and process.wait(timeout=20) == 0, result.stderr
            process = None
            log.close()
            log = None

        try:
            start(1)
            before = client('read')
            assert before['desired']['description'] == before['applied']['description'] == 'Loaded text'
            client('set', description='Saved text')
            saved = client('read')
            assert saved['desired'] == saved['persisted'] and saved['persisted']['description'] == 'Saved text'
            assert saved['applied']['description'] == 'Loaded text' and saved['apply_state'] == 'restart_required'
            gui.open_association(peer, 'Loaded text')
            assert 'Loaded text' in gui.query(peer, 'controls')
            (evidence / 'saved-active-controls.txt').write_text(gui.query(peer, 'controls'))
            gui.key(peer, ord('w'), 2)
            backup = types.with_suffix('.saved')
            original = types.read_bytes()
            types.rename(backup)
            types.mkdir()
            client('set', description='Failed text', success=False)
            assert backup.read_bytes() == original
            types.rmdir()
            backup.rename(types)
            gui.wait_for(lambda: client('read')['apply_state'], 'restart_required')
            old_stat = types.stat()
            types.write_bytes(types.read_bytes().replace(b'Saved text', b'Other text'))
            os.utime(types, ns=(old_stat.st_atime_ns, old_stat.st_mtime_ns))
            assert types.stat().st_size == old_stat.st_size
            gui.wait_for(lambda: client('read')['persisted']['description'], 'Other text')
            assert client('read')['applied']['description'] == 'Loaded text'
            client('upsert', 'application/x-runtime', 'Runtime one')
            added = client('read', 'application/x-runtime')
            assert added['persisted']['description'] == 'Runtime one' and added['applied'] == {} and added['apply_state'] == 'restart_required'
            client('delete', 'application/x-runtime')
            deleted = client('read', 'application/x-runtime')
            assert deleted['persisted'] == deleted['applied'] == {} and deleted['apply_state'] == 'applied'
            client('set', description='Bad\nrow', success=False)
            client('replace', description='Batch text')
            text = types.read_text()
            assert '# retained comment\nopaque extension line\n' in text and 'future:column' in text and 'Runtime added' in text
            assert client('read')['applied']['description'] == 'Loaded text'
            stop()
            start(2)
            restarted = client('read')
            assert restarted['desired'] == restarted['persisted'] == restarted['applied'] and restarted['applied']['description'] == 'Batch text' and restarted['apply_state'] == 'applied'
            gui.open_association(peer, 'Batch text')
            (evidence / 'restart-controls.txt').write_text(gui.query(peer, 'controls'))
            assert client('read', 'application/x-runtime')['applied']['description'] == 'Runtime added'
            client('delete')
            deleted = client('read')
            assert deleted['persisted'] == {} and deleted['applied']['description'] == 'Batch text' and deleted['apply_state'] == 'restart_required'
            stop()
            (evidence / 'result.json').write_text(json.dumps({'status': 'pass', 'checks': ['relocated-sdk', 'durable-save', 'saved-versus-active', 'native-gui-restart-policy', 'failed-save-no-effects', 'same-stat-reload', 'upsert', 'delete', 'batch', 'unknown-content-retention', 'invalid-input', 'desktop-restart'], 'history': history}, indent=2) + '\n')
            print('PASS file definitions: SDK save reload add delete batch failure and desktop restart')
        finally:
            (evidence / 'history.json').write_text(json.dumps(history, indent=2) + '\n')
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
