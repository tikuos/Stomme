#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Mark owned build output and update its configuration prerequisite."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil


def configure(output, host, flags, clean=False):
    """Refuse unowned output directories and invalidate objects when flags change."""
    if output.is_symlink():
        raise RuntimeError('Build output is a symbolic link')
    marker = output / '.foundation-build.json'
    if output.exists() and not marker.exists() and any(output.iterdir()):
        raise RuntimeError('Build output has no foundation marker: ' + str(output))
    if clean:
        if marker.is_file():
            shutil.rmtree(output)
        return ''
    output.mkdir(parents=True, exist_ok=True)
    stamp = output / 'configuration.stamp'
    data = dict(format=1, host=host, flags=flags)
    encoded = json.dumps(data, sort_keys=True) + '\n'
    if not marker.exists() or marker.read_text() != encoded:
        marker.write_text(encoded)
        stamp.write_text(hashlib.sha256(encoded.encode()).hexdigest() + '\n')
    return str(stamp)


def main():
    """Configure one output directory or remove a marked directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', default='')
    parser.add_argument('--flags', default='')
    parser.add_argument('--clean', action='store_true')
    args = parser.parse_args()
    print(configure(args.output, args.host, args.flags, args.clean))


if __name__ == '__main__':
    main()
