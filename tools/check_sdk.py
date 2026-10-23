#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check SDK ownership, header closure and compilation without source includes."""
from pathlib import Path
import argparse
import json
import re
import shlex
import subprocess
import tempfile
import hashlib
from kit_packages import BASE_KITS, validate_selection


def check(sdk):
    """Compile the public header set and reject private or document providers."""
    metadata = json.loads((sdk / 'sdk.json').read_text())
    profile = json.loads((Path(__file__).resolve().parents[1] / 'foundation.json').read_text())
    assert metadata['foundation_id'] == profile['id']
    provider = metadata['document_provider']
    kits = set(metadata['kit_names'].split())
    assert set(BASE_KITS) <= kits
    validate_selection(provider, kits)
    assert not list(sdk.rglob('*.c')) and not list(sdk.rglob('*_private.h'))
    headers = sorted((sdk / 'include').glob('*.h'))
    if provider == 'none':
        assert not any(p.name.startswith(('tiku_rich', 'tiku_deck')) for p in headers)
    else:
        assert (sdk / 'include/tiku_richdoc.h').is_file()
    for folder, key in [('include', 'header_sha256'), ('lib', 'archive_sha256')]:
        for name, expected in metadata[key].items():
            assert hashlib.sha256((sdk / folder / name).read_bytes()).hexdigest() == expected, name
    names = {p.name for p in headers}
    for header in headers:
        includes = re.findall(r'^\s*#\s*include\s*"([^"]+)"', header.read_text(), re.M)
        assert set(includes) <= names, (header.name, includes)
    assert {p.name for p in (sdk / 'lib').glob('*.a')} == set(metadata['libraries'])
    make = (sdk / 'sdk.mk').read_text()
    flags = next(line.split(' := ', 1)[1] for line in make.splitlines() if line.startswith('SDK_CFLAGS := '))
    flags = shlex.split(flags.replace('$(SDK_ROOT)', str(sdk)))
    with tempfile.TemporaryDirectory(prefix='foundation-sdk-headers-') as temp:
        source = Path(temp) / 'headers.c'
        source.write_text(''.join('#include "' + p.name + '"\n' for p in headers))
        subprocess.run(['cc', *flags, '-fsyntax-only', str(source)], check=True)
    print('SDK header closure and compilation:', len(headers), 'public headers;', provider)


def main():
    """Validate one installed or exported SDK directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=Path)
    check(parser.parse_args().sdk.resolve())


if __name__ == '__main__':
    main()
