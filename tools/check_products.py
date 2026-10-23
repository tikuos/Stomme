#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Run the desktop adoption matrix for explicitly supplied product checkouts."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

TESTS = ('test_doc_html', 'test_doc_css', 'test_doc_fonts', 'test_doc_flow',
         'test_doc_boxes', 'test_doc_images', 'test_doc_tables', 'test_html_view',
         'test_help_reader', 'test_document_reader', 'test_reader_allocations',
         'test_textfield_clip', 'test_clipboard_typed', 'test_remote_snapshot',
         'test_shm', 'test_win', 'test_text_input', 'test_ttf', 'test_fontset',
         'test_richdoc_model', 'test_richview', 'test_richio_native',
         'test_notes', 'test_write', 'test_word', 'test_access', 'test_print_rpc')
APPLICATION_TESTS = ('test_calc', 'test_calculator', 'test_help_window',
                     'test_reader_large', 'test_reader_reliability',
                     'test_reader_search_faults', 'test_image_viewer', 'test_pdf_viewer',
                     'test_archive_viewer', 'test_diff_viewer', 'test_grab_app',
                     'test_hex', 'test_terminal', 'test_circuit_viewer', 'test_marker',
                     'test_editor_document', 'test_editor_resize', 'test_editor_baseline')
TESTS = tuple(dict.fromkeys(TESTS + APPLICATION_TESTS))
WORKFLOWS = ('about.script', 'helpreader.script', 'documentreader.script',
             'fonts.script', 'notes.script', 'write.script', 'word.script')


def main():
    """Run serially within each build tree and retain command results."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--product', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--skip-workflows', action='store_true')
    args = parser.parse_args()
    foundation = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    revisions = set()
    env = dict(os.environ)
    for name in ('TRK_PORT', 'TRK_P4_PORT', 'TRK_BLE', 'TIKUBENCH_JLINK_SN'):
        env.pop(name, None)
    for index, product in enumerate(args.product):
        product = product.resolve()
        out = args.output / (str(index) + '-' + product.name)
        out.mkdir(exist_ok=True)
        make = ['make', 'FOUNDATION_ROOT=' + str(foundation)]

        def step(label, command, extra=None):
            started = time.time()
            with (out / (label + '.log')).open('wb') as stream:
                try:
                    code = subprocess.run(command, cwd=product, env=dict(env, **(extra or {})),
                                          stdout=stream, stderr=subprocess.STDOUT, timeout=900).returncode
                except subprocess.TimeoutExpired:
                    code = 124
            results.append(dict(product=str(product), label=label, command=command,
                                exit=code, seconds=round(time.time() - started, 3)))
            (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(product.name, label, code, flush=True)
            if code:
                raise RuntimeError('Product matrix failed: ' + str(out / (label + '.log')))

        step('build', make + ['-j' + str(args.jobs), 'core', 'sdk', 'apps', 'shells'])
        sdk = json.loads((product / 'build/sdk/sdk.json').read_text())
        revisions.add(sdk['foundation_revision'])
        shutil.copy2(product / 'build/sdk/sdk.json', out / 'sdk.json')
        shutil.copy2(product / 'build/foundation/sources.json', out / 'sources.json')
        tests = [name for name in TESTS if any((product / 'build/foundation/tests' / group / (name + '.c')).is_file()
                                              for group in ('kits', 'apps', 'system'))]
        profile = json.loads((product / 'foundation.product.json').read_text())
        if profile.get('shell') == 'desktop':
            tests = list(dict.fromkeys(tests + [p.stem for p in sorted((product / 'build/foundation/tests/system').glob('test_*.c'))]))
        step('build-tests', make + ['-j' + str(args.jobs)] + ['build/tests/' + name for name in tests])
        for name in tests:
            step(name, ['./build/tests/' + name])
        step('architecture', make + ['check-architecture', 'check-shipping', 'check-font'])
        step('html-sdk', ['python3', 'tests/integration/run_html_sdk.py', '--evidence', str(out / 'html-sdk')])
        if not args.skip_workflows:
            for name in WORKFLOWS:
                if (product / 'tests/integration/shell' / name).is_file():
                    step('workflow-' + name, ['sh', 'tests/integration/shell/run.sh'], {'TRK_SHELL_ONLY': name})
    if len(revisions) != 1:
        raise RuntimeError('Products adopted different foundation revisions')
    print('Product matrix passed for foundation', next(iter(revisions)))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
