#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Write a source and configuration identity for a standalone foundation SDK."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
from build_config import configure
from product_bridge import digest_files, source_files, write_changed, identity_header


def generate(root, output, provider, host, flags):
    """Pair compiled libraries with the selected provider and build flags."""
    if output.parent.is_symlink():
        raise RuntimeError('Build output is a symbolic link')
    if not (output.parent / '.foundation-build.json').exists():
        configure(output.parent, host, flags)
    revision = digest_files(source_files(root))
    data = dict(foundation_id=json.loads((root / 'foundation.json').read_text())['id'],
                foundation_revision=revision, document_provider=provider,
                presentation_provider='none', system=platform.system(),
                machine=platform.machine(), host=host, abi=5, flags=flags)
    data['sdk_identity'] = hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()
    write_changed(output / 'identity.json', (json.dumps(data, indent=2) + '\n').encode())
    write_changed(output / 'tiku_sdk_identity.h', identity_header(data))
    return data['sdk_identity']


def main():
    """Generate metadata before the compiler command stamp is selected."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--provider', choices=['none', 'tiku-basic'], required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--flags', default='')
    args = parser.parse_args()
    print(generate(Path(__file__).resolve().parents[1], args.output, args.provider, args.host, args.flags))


if __name__ == '__main__':
    main()
