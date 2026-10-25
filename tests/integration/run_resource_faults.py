#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Instrument resource allocation failures using selected source and public archives."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(args):
    """Build instrumented resource implementations and execute allocation refusals."""
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    sdk = args.sdk.resolve()
    with tempfile.TemporaryDirectory(prefix='resource-faults-') as directory:
        work = Path(directory)
        sources = [ROOT / 'tests/integration/resource_faults.c',
                   *sorted((ROOT / 'kits/storage').glob('tiku_resource*.c')),
                   *sorted((ROOT / 'kits/support').glob('tiku_resource*.c')),
                   ROOT / 'kits/support/tiku_msg.c']
        if args.registry_source:
            sources = [args.registry_source.resolve() if path.name == 'tiku_resource_registry.c' else path for path in sources]
        flags = ['cc', '-std=gnu11', '-D_DEFAULT_SOURCE', '-O1', '-g',
                 '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                 '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                 '-I' + str(sdk / 'include'), '-I' + str(ROOT / 'kits/storage'),
                 '-I' + str(ROOT / 'kits/support')]
        cmd = [*flags, *map(str, sources), '-Wl,--start-group',
               *map(str, sorted((sdk / 'lib').glob('*.a'))), '-Wl,--end-group',
               '-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc',
               '-Wl,--wrap=strdup', '-lm', '-pthread', '-o', str(work / 'faults')]
        built = subprocess.run(cmd, capture_output=True, text=True)
        (evidence / 'build.log').write_text(built.stdout + built.stderr)
        assert not built.returncode, built.stderr
        result = subprocess.run([str(work / 'faults')], capture_output=True, text=True, timeout=60)
        (evidence / 'run.log').write_text(result.stdout + result.stderr)
        (evidence / 'result.json').write_text(json.dumps(dict(command=cmd, exit=result.returncode), indent=2) + '\n')
        print(result.stdout, end='')
        assert not result.returncode, result.stderr


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--registry-source', type=Path)
    run(parser.parse_args())
