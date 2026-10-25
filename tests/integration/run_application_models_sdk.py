#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise native application providers through a relocated public SDK."""
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
import zipfile
import run_settings_sdk as gui

ROOT = Path(__file__).resolve().parents[2]


class Counter(str):
    """Unsigned decimal argument encoded using the public counter value type."""


def run(args):
    """Use separate native application processes and inspect their owned resources."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    history = []
    with tempfile.TemporaryDirectory(prefix='stomme-models-') as directory:
        work = Path(directory)
        sdk = work / 'sdk'
        shutil.copytree(args.sdk.resolve(), sdk)
        client=(ROOT/'tests/integration/shell_resources_client.c').read_text()
        client=client.replace("else if(arguments[i][0]=='i'&&arguments[i][1]==':')", "else if(arguments[i][0]=='u'&&arguments[i][1]==':')value=tiku_resource_value_string(TIKU_RESOURCE_COUNTER,arguments[i+1]);\n            else if(arguments[i][0]=='i'&&arguments[i][1]==':')")
        (work/'client.c').write_text(client)
        shutil.copy2(ROOT / 'examples/resource_client.c', work / 'stop.c')
        settings = (ROOT / 'examples/settings_client.c').read_text()
        settings = settings.replace('"durability", "user"',
                                    '"durability", getenv("STOMME_TEST_DURABILITY") ? getenv("STOMME_TEST_DURABILITY") : "user"')
        settings = settings.replace('"setting job: %s\\n", state ? state : "invalid response"',
                                    '"settings job: %s %s %s\\n", state ? state : "invalid", tiku_msg_find_string(result,"code",0), tiku_msg_find_string(result,"effect",0)')
        (work / 'settings.c').write_text(settings)
        (work / 'fixture.c').write_text('''#include "tiku_richio.h"
int main(int argc,char **argv){tiku_richdoc_t *doc=0;tiku_richsnapshot_t *snapshot=0;
if(argc!=2||tiku_richio_read_text("baseline",8,0,&doc)||tiku_richdoc_snapshot(doc,&snapshot))return 1;
int error=tiku_richio_save(argv[1],snapshot,0);tiku_richsnapshot_release(snapshot);tiku_richdoc_release(doc);return error?1:0;}
''')
        (work / 'Makefile').write_text('include ' + str(sdk / 'sdk.mk') + '\n'
            'KITS := $(foreach kit,$(SDK_KIT_NAMES),$(SDK_ROOT)/lib/libtiku_$(kit).a)\n'
            'all: client settings stop fixture\n%: %.c\n'
            '\t$(CC) -o $@ $(SDK_CFLAGS) $< $(KITS) $(SDK_LDFLAGS)\n')
        built = subprocess.run(['make', '-j4'], cwd=work, capture_output=True, text=True)
        (evidence / 'sdk-build.log').write_text(built.stdout + built.stderr)
        assert not built.returncode, built.stderr
        home = work / 'home'
        (home / 'Desktop').mkdir(parents=True)
        source = home / 'document.txt'
        source.write_text('baseline')
        rich = home / 'document.tkrd'
        subprocess.run([str(work / 'fixture'), str(rich)], check=True)
        control, driver = work / 'control', work / 'driver.sock'
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
                   XDG_DATA_HOME=str(home / '.local/share'),
                   XDG_CACHE_HOME=str(home / '.cache'), TIKU_APPS=str(args.apps.resolve()),
                   STOMME_RESOURCE_DIRECTORY=str(control), STOMME_RESOURCE_TEST_PROVIDER='1',
                   STOMME_RESOURCE_MOUNT='/application-' + str(os.getpid()))
        env.pop('DISPLAY', None)
        process = peer = None
        group = None
        log = (evidence / 'desktop.log').open('w')

        def call(operation, path, *values, success=True):
            """Record the public request and require its exact success classification."""
            result = subprocess.run([str(work / 'client'), str(control), operation,
                                     str(path), *map(str, values)], capture_output=True,
                                    text=True, timeout=20)
            history.append(dict(operation=operation, path=str(path), values=values,
                                exit=result.returncode, stdout=result.stdout,
                                stderr=result.stderr))
            assert (result.returncode == 0) == success, history[-1]
            return result.stdout.strip()

        def read(path):
            """Read the latest applied native snapshot through a separate SDK client."""
            result = call('read', path)
            applied = result.split(',"applied":', 1)[1].rsplit('\n}', 1)[0].rstrip('\n')
            try:
                return json.loads(applied)
            except json.JSONDecodeError:
                return applied

        def children(path):
            """Retry read-only page cursors while native providers publish."""
            for attempt in range(20):
                try:
                    return [line.split('\t')[0] for line in call('list', path).splitlines()]
                except AssertionError:
                    if history[-1]['stderr'].strip() != 'stale_cursor':
                        raise
                    time.sleep(.05)
            raise AssertionError(('unstable collection', path))

        def action(path, values=None, success=True):
            """Retry pre-dispatch contention without replaying uncertain native effects."""
            for attempt in range(40):
                fields = ['object']
                arguments = values() if callable(values) else values
                for key, value in (arguments or {}).items():
                    fields.extend([('u:' if isinstance(value,Counter) else 'i:' if isinstance(value, int) else 't:') + key, str(value)])
                try:
                    result = call('action-result', path, *fields, success=success)
                    return json.loads(result) if success else None
                except AssertionError:
                    if not success or history[-1]['stderr'].strip() not in ('stale_revision', 'busy', 'settings job: failed stale_revision none', 'settings job: failed busy none'):
                        raise
                    time.sleep(.05)
            raise AssertionError(history[-1])

        def prefix(app):
            """Discover an assigned provider by its native manifest identity."""
            for base in children('/apps'):
                if base == '/apps/catalog':
                    continue
                if base + '/identity' not in children(base):
                    continue
                identity = read(base + '/identity')
                if identity.get('kind') == 'application_provider' and identity.get('id') == 'org.tikuos.' + {'editor': 'edit'}.get(app, app):
                    return base
            return None

        def wait_value(function):
            """Wait for an owned publication, tolerating retired transient document paths."""
            until = time.monotonic() + 30
            while time.monotonic() < until:
                try:
                    value = function()
                    if value:
                        return value
                except (AssertionError, KeyError, json.JSONDecodeError):
                    if not history or history[-1]['stderr'].strip() not in ('not_found', 'stale_revision', 'owner_lost', ''):
                        raise
                assert process.poll() is None, 'desktop exited during native workflow'
                time.sleep(.05)
            raise AssertionError(('publication deadline', history[-5:]))

        def launch(app, path=None):
            """Launch a native module through its manifest-owned catalog action."""
            action('/apps/catalog/' + app + '/actions/' + ('open' if path else 'launch'),
                   {'path': str(path)} if path else {})
            return wait_value(lambda: prefix(app))

        def document(base, path=None):
            """Resolve a current owner document after asynchronous file adoption."""
            def resolve():
                for candidate in children(base + '/documents'):
                    identity = read(candidate + '/identity')
                    if not identity.get('busy') and (path is None or read(candidate + '/path') == str(path)):
                        return candidate
                return None
            return wait_value(resolve)

        def command(doc, name, **values):
            """Capture the current selection token before one native document action."""
            return action(doc + '/commands/' + name,
                          lambda: dict(context=read(doc + '/selection')['context'], **values))

        def setting(path, kind, value, durability='session'):
            """Use the public replacement client with the descriptor's persistence scope."""
            for attempt in range(40):
                result = subprocess.run([str(work / 'settings'), str(control), path, kind,
                                         str(value)], capture_output=True, text=True,
                                        env=dict(env, STOMME_TEST_DURABILITY=durability), timeout=20)
                history.append(dict(operation='set', path=path, value=value, exit=result.returncode,
                                    stdout=result.stdout, stderr=result.stderr))
                if not result.returncode:
                    return
                assert result.stderr.strip() in ('stale_revision','busy','settings job: failed stale_revision none','settings job: failed busy none'),history[-1]
                time.sleep(.05)
            raise AssertionError(history[-1])

        try:
            process = subprocess.Popen([str(args.desktop.resolve()), '-mute', '-conduct',
                                        str(driver), str(home / 'Desktop')], cwd=work,
                                       env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            group = process.pid
            until = time.monotonic() + 30
            while not (control / 'control.sock').exists() or not driver.exists():
                assert process.poll() is None and time.monotonic() < until, 'desktop startup failed'
                time.sleep(.02)
            peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            peer.settimeout(5)
            peer.connect(str(driver))
            gui.send(peer, 1, struct.pack('=I', 2))
            time.sleep(.5)
            editor = launch('editor', source)
            doc = document(editor, source)
            command(doc, 'replace-text', text='added')
            wait_value(lambda: read(doc + '/dirty'))
            assert command(doc, 'read-text', offset=0, limit=4096)['text'] == 'addedbaseline'
            command(doc, 'undo')
            wait_value(lambda: not read(doc + '/dirty'))
            command(doc, 'redo')
            wait_value(lambda: read(doc + '/dirty'))
            command(doc, 'save')
            wait_value(lambda: source.read_text() == 'addedbaseline')
            wait_value(lambda: not read(doc + '/dirty'))
            setting(editor + '/preferences/tab', 'integer', 8, 'user')
            assert read(editor + '/preferences/tab') == 8
            write = launch('write', rich)
            rich_doc = document(write, rich)
            assert command(rich_doc, 'read-objects', offset=0, limit=32)['objects']
            command(rich_doc, 'text.replace', payload='rich ')
            wait_value(lambda: read(rich_doc + '/dirty'))
            assert command(rich_doc, 'read-text', offset=0, byte=0, limit=4096)['text'] == 'rich baseline'
            command(rich_doc, 'history.undo')
            wait_value(lambda: not read(rich_doc + '/dirty'))
            command(rich_doc, 'history.redo')
            command(rich_doc, 'save')
            wait_value(lambda: not read(rich_doc + '/dirty'))
            with zipfile.ZipFile(rich) as archive:
                assert b'rich baseline' in archive.read('document.xml')
            setting(rich_doc + '/view/show_ruler', 'boolean', 'true')
            assert read(rich_doc + '/view/show_ruler') is True
            notes = launch('notes')
            note = document(notes)
            setting(note + '/view/important', 'boolean', 'true')
            assert read(note + '/view/important') is True
            command(note, 'text.replace', payload='retained note')
            wait_value(lambda: not read(note + '/dirty'))
            assert command(note, 'read-text', offset=0, byte=0, limit=4096)['text'] == 'retained note'
            report = dict(editor=True, write=True, notes=True, relocated_sdk=True,
                          native_save_undo_redo=True, separate_processes=True)
            if (args.apps / 'word').is_dir():
                word = launch('word', rich)
                word_doc = document(word, rich)
                commands = children(word + '/commands')
                assert len([path for path in commands if path not in [word + '/commands/entries', word + '/commands/invoke']]) == 450
                selection = read(word_doc + '/selection')
                command(word_doc,'select',anchor_body=Counter(selection['anchor_body']),anchor_paragraph=Counter(selection['anchor_paragraph']),anchor_byte=Counter('0'),caret_body=Counter(selection['anchor_body']),caret_paragraph=Counter(selection['anchor_paragraph']),caret_byte=Counter(str(len('rich baseline'))))
                identity = read(word_doc + '/identity')
                action(word + '/commands/format.bold', lambda: dict(window=identity['window'], context=read(word_doc+'/selection')['context']))
                wait_value(lambda: read(word_doc+'/dirty'))
                command(word_doc,'history.undo')
                wait_value(lambda: not read(word_doc+'/dirty'))
                report['word_commands'] = 450
                setting(word+'/preferences/zoom','integer',125,'user')
                wait_value(lambda: read(word+'/preferences/zoom')==125)
                bindings=read(word+'/preferences/bindings')
                assert isinstance(bindings,list)
                wait_value(lambda: json.loads(call('read',word+'/preferences/zoom'))['persisted']==125)
                for key,array in [('binding_count','bindings'),('hidden_count','hidden'),('quick_count','quick'),('recent_count','recent')]:
                    assert read(word+'/preferences/'+key)==len(read(word+'/preferences/'+array))
                assert read(word+'/preferences/recent_count')>0
                setting(word+'/preferences/pinned','integer',1,'user')
                wait_value(lambda: read(word+'/preferences/pinned')==1)
                report['word_preferences']=47
                action(word+'/commands/file.close',lambda: dict(window=identity['window'],context=read(word_doc+'/selection')['context']))
                wait_value(lambda: prefix('word') is None)
                report['word_close_acknowledged']=True
            if (args.apps / 'deck').is_dir():
                deck = launch('deck')
                presentation = document(deck)
                assert len(children(presentation+'/commands'))==69
                command(presentation, 'slide-new')
                wait_value(lambda: read(presentation + '/dirty'))
                command(presentation, 'undo')
                report['deck_native_history'] = True
            action(editor + '/commands/invoke', dict(window=read(doc + '/identity')['window'], command=3))
            wait_value(lambda: prefix('editor') is None)
            call('read', doc + '/identity', success=False)
            report['disconnect_retirement'] = True
            stopped=subprocess.run([str(work/'stop'),str(control),'--stop'],capture_output=True,text=True,timeout=15)
            history.append(dict(operation='stop',exit=stopped.returncode,stdout=stopped.stdout,stderr=stopped.stderr))
            assert not stopped.returncode,history[-1]
            assert process.wait(timeout=20) == 0
            process = None
            (evidence / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(report, indent=2))
        finally:
            (evidence / 'requests.json').write_text(json.dumps(history, indent=2) + '\n')
            if peer:
                peer.close()
            if process:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if group:
                try:
                    os.killpg(group,signal.SIGTERM)
                except ProcessLookupError:
                    pass
                time.sleep(.2)
            log.close()


def main():
    """Select an exported product SDK and its installed native modules."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--desktop', type=Path, required=True)
    parser.add_argument('--apps', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    run(parser.parse_args())


if __name__ == '__main__':
    main()
