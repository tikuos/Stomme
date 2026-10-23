#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Export selected shared kit headers, PIC archives and relocatable metadata."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import re
import shlex
import shutil
import tempfile
from kit_packages import LINK_ORDER, OPTIONAL_KITS, REQUIRES, file_kit, validate_selection

ROOT = Path(__file__).resolve().parents[1]
INCLUDES = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def public_headers(root, kits):
    """Return headers whose quoted include graph is public and complete."""
    headers = {}
    paths = [(str(path.relative_to(root)), path) for path in (root / 'kits').glob('*/*.h')]
    package = root / 'packages/document-basic'
    if 'document' in kits:
        paths += [(str(path.relative_to(package)), path) for path in (package / 'kits').glob('*/*.h')]
    for name, path in paths:
        if file_kit(name) not in kits:
            continue
        if path.name in headers:
            raise RuntimeError('Duplicate header: ' + path.name)
        headers[path.name] = path
    rejected = {name for name in headers if '_private' in name}
    dependencies = {name: set(INCLUDES.findall(p.read_text())) for name, p in headers.items()}
    while True:
        following = {name for name in headers
                     if dependencies[name] & rejected or dependencies[name] - headers.keys()}
        if following <= rejected:
            break
        rejected |= following
    return {name: p for name, p in headers.items() if name not in rejected}


def compiler_flags(flags):
    """Remove build tuning and relocate the Darwin compatibility include."""
    result = []
    for value in shlex.split(flags):
        if value.startswith(('-O', '-g', '-ffile-prefix-map=')):
            continue
        if value.startswith('kits/') or value.endswith('/tiku_sdk_identity.h'):
            value = '$(SDK_ROOT)/include/' + Path(value).name
        if str(ROOT) in value:
            raise RuntimeError('SDK flags reference the source checkout')
        result.append(value)
    return result


def publish(stage, destination, identity):
    """Replace an SDK owned by this foundation identity."""
    previous = destination.with_name('.' + destination.name + '.previous')
    if destination.is_symlink() or previous.exists() or previous.is_symlink():
        raise RuntimeError('SDK output is a symlink or has a previous export')
    if destination.exists():
        marker = destination / 'sdk.json'
        if not marker.is_file() or json.loads(marker.read_text()).get('foundation_id') != identity:
            raise RuntimeError('SDK output is not owned by this foundation')
        destination.rename(previous)
    try:
        stage.rename(destination)
    except OSError:
        if previous.exists():
            previous.rename(destination)
        raise
    if previous.exists():
        shutil.rmtree(previous)


def export(args):
    """Publish public headers and matching archives for the selected provider."""
    profile = json.loads((ROOT / 'foundation.json').read_text())
    provider = getattr(args, 'provider', 'none')
    kits = [k for k in LINK_ORDER if k in profile['kits'] or provider == 'tiku-basic' and k in {'document_formats', 'document'}]
    validate_selection(provider, kits)
    output = args.output.absolute()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.foundation-sdk-', dir=output.parent) as temp:
        stage = Path(temp) / 'sdk'
        if args.copy:
            data = json.loads((args.copy / 'sdk.json').read_text())
            if data.get('foundation_id') != profile['id']:
                raise RuntimeError('Installation source belongs to another provider')
            shutil.copytree(args.copy, stage)
        else:
            libraries = {p.stem.removeprefix('libtiku_'): p for p in args.libraries}
            if set(libraries) != set(kits) or len(args.libraries) != len(kits):
                raise RuntimeError('SDK requires exactly one archive for each selected kit')
            headers = public_headers(ROOT, kits)
            (stage / 'include').mkdir(parents=True)
            (stage / 'lib/pkgconfig').mkdir(parents=True)
            for name, source in headers.items():
                shutil.copy2(source, stage / 'include' / name)
            for kit in kits:
                shutil.copy2(libraries[kit], stage / 'lib' / libraries[kit].name)
            shutil.copytree(ROOT / 'licenses', stage / 'licenses')
            for kit in kits:
                for source in (ROOT / 'kits' / kit).glob('LICENSE*'):
                    target = stage / 'licenses' / kit
                    target.mkdir(exist_ok=True)
                    shutil.copy2(source, target / source.name)
            for name in ['LICENSE', 'NOTICE']:
                shutil.copy2(ROOT / name, stage / 'licenses' / name)
            abi = int(re.search(r'#define\s+TIKU_APP_ABI\s+(\d+)', headers['tiku_app.h'].read_text())[1])
            source_hash = hashlib.sha256()
            for source in sorted((ROOT / 'kits').rglob('*')):
                if source.is_file() and '__pycache__' not in source.parts:
                    source_hash.update(str(source.relative_to(ROOT)).encode() + b'\0' + source.read_bytes())
            metadata = dict(version=profile['version'], product=profile['name'],
                            foundation_id=profile['id'], document_provider=provider,
                            abi=abi, system=platform.system(), machine=platform.machine(), host=args.host,
                            source_sha256=source_hash.hexdigest(), kit_names=' '.join(kits),
                            libraries=[libraries[k].name for k in kits])
            if getattr(args, 'identity', None):
                identity = json.loads((args.identity / 'identity.json').read_text())
                if identity['document_provider'] != provider or identity['host'] != args.host:
                    raise RuntimeError('SDK identity differs from the selected provider or host')
                metadata.update({k: v for k, v in identity.items() if k != 'flags'})
                shutil.copy2(args.identity / 'tiku_sdk_identity.h', stage / 'include/tiku_sdk_identity.h')
            metadata['header_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in (stage / 'include').glob('*.h')}
            metadata['archive_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                          for p in libraries.values()}
            (stage / 'sdk.json').write_text(json.dumps(metadata, indent=2) + '\n')
            cflags = ['-I$(SDK_ROOT)/include', *compiler_flags(args.cflags)]
            ldflags = shlex.split(args.ldflags)
            if any(str(ROOT) in f for f in ldflags):
                raise RuntimeError('SDK link flags reference the source checkout')
            config = ['# Shared kit SDK; paths follow this file.\n',
                      'SDK_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))\n']
            for key in ['version', 'abi', 'system', 'machine', 'host', 'kit_names',
                        'foundation_id', 'document_provider', 'foundation_revision', 'sdk_identity']:
                config.append('SDK_' + key.upper() + ' := ' + str(metadata.get(key, "")) + '\n')
            config += ['SDK_CFLAGS := ' + shlex.join(cflags) + '\n',
                       'SDK_LDFLAGS := ' + shlex.join(ldflags) + '\n']
            (stage / 'sdk.mk').write_text(''.join(config))
            pc = ['prefix=${pcfiledir}/../..', 'libdir=${prefix}/lib', 'includedir=${prefix}/include',
                  'Name: ' + profile['name'], 'Description: Shared native desktop kit SDK',
                  'Version: ' + profile['version'],
                  'Cflags: ' + shlex.join(cflags).replace('$(SDK_ROOT)', '${prefix}'),
                  'Libs: -L${libdir} ' + ' '.join('-ltiku_' + k for k in kits if k not in OPTIONAL_KITS) + ' ' + shlex.join(ldflags)]
            (stage / 'lib/pkgconfig' / (profile['id'] + '.pc')).write_text('\n'.join(pc) + '\n')
            for kit in kits:
                if kit not in OPTIONAL_KITS:
                    continue
                closure = {kit}
                while True:
                    expanded = closure | set().union(*(REQUIRES.get(k, set()) for k in closure))
                    if expanded == closure:
                        break
                    closure = expanded
                links = ' '.join('-ltiku_' + k for k in kits if k in closure or k not in OPTIONAL_KITS)
                content = '\n'.join(pc).replace(' '.join('-ltiku_' + k for k in kits if k not in OPTIONAL_KITS), links)
                (stage / 'lib/pkgconfig' / ('tiku-' + kit + '.pc')).write_text(content + '\n')
        publish(stage, output, profile['id'])
    print('Shared foundation SDK:', output)


def main():
    """Read the SDK destination and selected build artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--copy', type=Path)
    parser.add_argument('--provider', default='none', choices=['none', 'tiku-basic'])
    parser.add_argument('--identity', type=Path)
    parser.add_argument('--host', default='x11')
    parser.add_argument('--cflags', default='')
    parser.add_argument('--ldflags', default='')
    parser.add_argument('--libraries', type=Path, nargs='*', default=[])
    export(parser.parse_args())


if __name__ == '__main__':
    main()
