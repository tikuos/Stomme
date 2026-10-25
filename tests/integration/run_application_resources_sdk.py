#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compile optional native application resource callbacks against a relocated SDK."""
from pathlib import Path
import argparse
import ctypes
import json
import shutil
import subprocess
import tempfile


def main():
    """Build public entry points and verify the versioned callback exports."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='stomme-app-resources-') as directory:
        work = Path(directory)
        sdk = work / 'sdk'
        application = work / 'application'
        shutil.copytree(args.sdk.resolve(), sdk)
        shutil.copytree(root / 'examples/application-resources', application,
                        ignore=shutil.ignore_patterns('build'))
        assert not any(sdk.rglob('*.c')) and not any(sdk.rglob('*_private.h'))
        built = subprocess.run(['make', '-j2', 'SDK=' + str(sdk), 'all', 'smoke'],
                               cwd=application, capture_output=True, text=True,
                               timeout=120)
        (args.evidence / 'build.log').write_text(built.stdout + built.stderr)
        assert not built.returncode, built.stderr
        assert str(root / 'kits') not in built.stdout
        module = ctypes.CDLL(str(application / 'build/counter.so'))

        class Prefix(ctypes.Structure):
            """Public version and structure-size fields preceding owner callbacks."""
            _fields_ = [('version', ctypes.c_uint32), ('size', ctypes.c_uint32)]

        extension = Prefix.in_dll(module, 'tiku_app_resources_v1')
        assert extension.version == 1 and extension.size >= 8
        descriptor = Prefix.in_dll(module, 'tiku_app_v1')
        assert descriptor.version == 5
        result = dict(relocated=True, source_free=True, callbacks_version=1,
                      application_abi=descriptor.version, smoke=True)
        (args.evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('PASS relocated native application resource SDK example')


if __name__ == '__main__':
    main()
