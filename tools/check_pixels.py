#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compare a rendered scene with an independently recorded desktop fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def check(data, fixture):
    """Require the declared pixel extent and exact reference image digest."""
    return (len(data) == fixture['width'] * fixture['height'] * 4 and
            hashlib.sha256(data).hexdigest() == fixture['sha256'])


def main():
    """Run a pixel emitter and compare its output to the selected reference."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--program', type=Path, required=True)
    args = parser.parse_args()
    pixels = subprocess.check_output([str(args.program.resolve())])
    if not check(pixels, json.loads(args.fixture.read_text())):
        raise SystemExit('Rendered pixels differ from the desktop reference')
    print('Rendered pixels match desktop reference:', hashlib.sha256(pixels).hexdigest())


if __name__ == '__main__':
    main()
