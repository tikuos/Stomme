#!/usr/bin/env python3
"""Build and exercise the HTML example against a relocated, source-free SDK."""
from pathlib import Path
import argparse
import ctypes
import json
import shutil
import subprocess
import tempfile


def main():
    """Copy public artifacts, build both entry points and run the headless smoke."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--sdk', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='tiku-html-sdk-') as temp:
        temp = Path(temp)
        sdk = temp / 'relocated-sdk'
        example = temp / 'application'
        shutil.copytree(args.sdk or root / 'build/sdk', sdk)
        shutil.copytree(root / 'examples/html-view', example,
                        ignore=shutil.ignore_patterns('build'))
        assert not any(sdk.rglob('*_private.h'))
        assert not any(sdk.rglob('*.c'))
        result = subprocess.run(['make', '-j4', 'SDK=' + str(sdk), 'all', 'smoke'],
                                cwd=example, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=90)
        if args.evidence:
            args.evidence.mkdir(parents=True, exist_ok=True)
            (args.evidence / 'detached-build.log').write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(result.stdout)
        assert str(root / 'kits') not in result.stdout
        assert str(root / 'apps') not in result.stdout
        module = ctypes.CDLL(str(example / 'build/htmlview.so'))

        class Export(ctypes.Structure):
            """The public module's ABI/version prefix."""
            _fields_ = [('abi', ctypes.c_uint32), ('size', ctypes.c_uint32),
                        ('descriptor', ctypes.c_void_p)]

        exported = Export.in_dll(module, 'tiku_app_v1')
        assert exported.abi == json.loads((sdk / 'sdk.json').read_text())['abi']
        assert exported.size and exported.descriptor
        if args.evidence:
            shutil.copy2(example / 'build/preview.png', args.evidence / 'preview.png')
        print('HTML SDK: relocated standalone app, loadable module and smoke passed')


if __name__ == '__main__':
    main()
