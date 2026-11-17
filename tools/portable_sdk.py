#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Validate an installed portable SDK without foundation source access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys

LIBRARIES = ['libtiku_render.a', 'libtiku_models.a', 'libtiku_font_subset.a']


def elf_section(data, wanted):
    """Read a named ELF section from a regular 32/64-bit relocatable object."""
    if data[:4] != b'\x7fELF' or data[4] not in {1, 2} or data[5] not in {1, 2}:
        raise ValueError('Expected an ELF object')
    endian = '<' if data[5] == 1 else '>'
    wide = data[4] == 2
    offset = struct.unpack_from(endian + ('Q' if wide else 'I'), data, 40 if wide else 32)[0]
    stride, count, strings = struct.unpack_from(endian + 'HHH', data, 58 if wide else 46)
    if not count or strings >= count or stride < (64 if wide else 40) or offset + stride * count > len(data):
        raise ValueError('Invalid ELF section table')
    sections = [struct.unpack_from(endian + ('IIQQQQIIQQ' if wide else 'IIIIIIIIII'), data, offset + i * stride)
                for i in range(count)]
    names = sections[strings]
    table = data[names[4]:names[4] + names[5]]
    for section in sections:
        start = section[0]
        end = table.find(b'\0', start)
        if end >= start and table[start:end].decode('ascii') == wanted:
            if section[4] + section[5] > len(data):
                raise ValueError('Truncated ELF section')
            if wanted == '.tiku.identity' and section[2] & 2:
                raise ValueError('Object identity must not occupy runtime storage')
            return data[section[4]:section[4] + section[5]]
    raise ValueError('Missing ELF section: ' + wanted)


def archive_identities(path, ar, expected, members=None):
    """Reject unmarked, duplicated or stale members, including fat LTO objects."""
    names = subprocess.check_output(shlex.split(ar) + ['t', str(path)], text=True).splitlines()
    if not names or len(set(names)) != len(names) or (members is not None and names != members):
        raise ValueError('Archive member inventory differs: ' + path.name)
    marker = ('TIKU_PORTABLE_ID=' + expected).encode() + b'\0'
    for name in names:
        obj = subprocess.check_output(shlex.split(ar) + ['p', str(path), name])
        if elf_section(obj, '.tiku.identity') != marker:
            raise ValueError('Stale archive member: ' + path.name + '/' + name)
    return names


def validate(sdk, target=None, abi=None, required=(), cc=None):
    """Check target selection, capability requirements and exact exported files."""
    data = json.loads((sdk / 'sdk.json').read_text())
    if data.get('profile') != 'portable-ui' or data.get('format') != 1 or data.get('foundation_id') != 'desktop-foundation':
        raise ValueError('Not a portable foundation SDK')
    if data['target'] not in {'host-test', 's31'} or (target and target != data['target']):
        raise ValueError('Incompatible SDK target')
    if abi and abi != data['abi']:
        raise ValueError('Incompatible SDK ABI')
    exported = {'libraries', 'members', 'public_symbols', 'consumer_flags',
                'header_sources', 'files_sha256', 'sdk_identity'}
    identity = {key: value for key, value in data.items() if key not in exported}
    if hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest() != data['sdk_identity']:
        raise ValueError('SDK build identity differs')
    missing = set(required) - set(data['capabilities'])
    if missing:
        raise ValueError('Missing capabilities: ' + ', '.join(sorted(missing)))
    files = {str(p.relative_to(sdk)) for p in sdk.rglob('*') if p.is_file() and p.name != 'sdk.json'}
    if any(p.is_symlink() for p in sdk.rglob('*')) or files != set(data['files_sha256']):
        raise ValueError('SDK file inventory differs')
    for name, digest in data['files_sha256'].items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or hashlib.sha256((sdk / path).read_bytes()).hexdigest() != digest:
            raise ValueError('SDK checksum differs: ' + name)
    if data['libraries'] != LIBRARIES:
        raise ValueError('SDK archive selection differs')
    headers = {p.name for p in (sdk / 'include').glob('*.h')}
    if any(p.suffix == '.c' or '_private' in p.name for p in sdk.rglob('*')):
        raise ValueError('Implementation source in SDK')
    for path in (sdk / 'include').glob('*.h'):
        imports = set(re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.M))
        if not imports <= headers:
            raise ValueError('Incomplete header closure: ' + path.name)
    if cc:
        triple = subprocess.check_output(shlex.split(cc) + ['-dumpmachine'], text=True).strip()
        if triple != data['compiler_target']:
            raise ValueError('Compiler target differs: ' + triple)
        if '-flto' in data['flags']:
            version = subprocess.check_output(shlex.split(cc) + ['--version'], text=True).splitlines()[0]
            if version != data['compiler_version']:
                raise ValueError('LTO requires the recorded compiler version')
    return data


def main():
    """Print ok on compatible intact SDKs; report rejection on stderr."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--target', choices=['host-test', 's31'])
    parser.add_argument('--abi')
    parser.add_argument('--require', nargs='*', default=[])
    parser.add_argument('--cc')
    args = parser.parse_args()
    try:
        validate(args.sdk.resolve(), args.target, args.abi, args.require, args.cc)
    except (ValueError, KeyError, OSError, struct.error, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
