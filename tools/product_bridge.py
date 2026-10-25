#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compose product kit sources from a checked foundation and local providers."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys

from kit_packages import LINK_ORDER, file_kit, validate_profile

KITS = ('support', 'device', 'storage', 'interface', 'translation', 'application', 'media', 'editor')
HOST_FILES = {'tiku_sys_linux.c', 'tiku_sys_darwin.c', 'tiku_sound_pulse.c',
              'tiku_sound_coreaudio.c', 'tiku_cocoa.m', 'tiku_x11.c',
              'tiku_x11_clipboard.c', 'tiku_x11_command_linux.c',
              'tiku_x11_command_darwin.c', 'tiku_access_cocoa.m', 'tiku_print_cocoa.m',
              'tiku_access_linux.c', 'tiku_access_linux_methods.c',
              'tiku_access_linux_text.c', 'tiku_access_linux_table.c', 'tiku_print_linux.c'}


def digest_files(files):
    """Hash sorted relative names and bytes with unambiguous boundaries."""
    digest = hashlib.sha256()
    for name, path in sorted(files.items()):
        data = path.read_bytes()
        digest.update(name.encode() + b'\0' + str(len(data)).encode() + b'\0' + data)
    return digest.hexdigest()


def source_files(root):
    """Select implementation, generated-input and build-contract sources."""
    files = {str(p.relative_to(root)): p for p in (root / 'kits').rglob('*')
             if p.is_file() and '__pycache__' not in p.parts}
    files.update({str(p.relative_to(root)): p for p in (root / 'packages').rglob('*')
                  if p.is_file() and '__pycache__' not in p.parts})
    for pattern in ('foundation.json', 'Makefile', 'tools/*.py', 'tests/kits/**/*', 'tests/fixtures/**/*'):
        files.update({str(p.relative_to(root)): p for p in root.glob(pattern) if p.is_file()})
    return files


def identity_header(data):
    """Expose the selected SDK identity, foundation content revision and native host."""
    fields = [('TIKU_SDK_IDENTITY', 'sdk_identity'),
              ('TIKU_FOUNDATION_REVISION', 'foundation_revision'),
              ('TIKU_SDK_HOST', 'host')]
    return ''.join('#define ' + macro + ' ' + json.dumps(data[key]) + '\n'
                   for macro, key in fields).encode()


def write_changed(path, data):
    """Preserve a generated file's timestamp when its bytes match."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        temporary = path.with_name(path.name + '.tmp')
        temporary.write_bytes(data)
        temporary.replace(path)


def host_files(system, host):
    """Select platform backends for one runtime host."""
    if host not in {'x11', 'cocoa'} or (host == 'cocoa' and system != 'Darwin'):
        raise RuntimeError('Unsupported foundation host: ' + system + '/' + host)
    selected = {'tiku_sys_darwin.c', 'tiku_sound_coreaudio.c', 'tiku_access_cocoa.m',
                'tiku_print_cocoa.m'} if system == 'Darwin' else {
                    'tiku_sys_linux.c', 'tiku_sound_pulse.c', 'tiku_access_linux.c',
                    'tiku_access_linux_methods.c', 'tiku_access_linux_text.c',
                    'tiku_access_linux_table.c', 'tiku_print_linux.c'}
    selected |= {'tiku_cocoa.m'} if host == 'cocoa' else {
        'tiku_x11.c', 'tiku_x11_clipboard.c',
        'tiku_x11_command_darwin.c' if system == 'Darwin' else 'tiku_x11_command_linux.c'}
    return selected


def source_revision(root):
    """Return the checkout's commit, or None for an export or unborn checkout."""
    try:
        top = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'],
                             capture_output=True, text=True)
        if top.returncode or Path(top.stdout.strip()).resolve() != root.resolve():
            return None
        revision = subprocess.run(['git', '-C', str(root), 'rev-parse', '--verify', 'HEAD'],
                                  capture_output=True, text=True)
    except FileNotFoundError:
        return None
    return revision.stdout.strip() if not revision.returncode else None


def compose(root, product, output, host, flags, allow_dirty=False):
    """Validate the source pin and create symlinks, inventory and Make input."""
    lock = json.loads((product / 'foundation.lock.json').read_text())
    profile = json.loads((product / 'foundation.product.json').read_text())
    metadata = json.loads((root / 'foundation.json').read_text())
    provider, presentation = validate_profile(profile)
    actual = digest_files(source_files(root))
    if metadata['id'] != lock['foundation_id']:
        raise RuntimeError('Foundation identity differs from foundation.lock.json')
    if actual != lock['source_sha256'] and not allow_dirty:
        raise RuntimeError('Foundation source differs from foundation.lock.json; update the pin or set FOUNDATION_ALLOW_DIRTY=1 for a recorded development build')
    output.mkdir(parents=True, exist_ok=True)
    marker = output / '.foundation-view'
    if not marker.exists() and any(output.iterdir()):
        raise RuntimeError('Foundation view contains unrecognized output: ' + str(output))
    marker.touch(exist_ok=True)
    selected = {str(p.relative_to(product)): p for p in (product / 'kits').rglob('*')
                if p.is_file() and '__pycache__' not in p.parts}
    for name in selected:
        kit = file_kit(name)
        if kit in {'document', 'document_formats'} and provider == 'none' or kit in {'deck', 'deck_formats', 'office_shapes'} and presentation == 'none':
            raise RuntimeError('Unselected product provider: ' + name)
        if kit in {'document', 'document_formats'} and provider == 'tiku-basic':
            basic = root / metadata.get('optional_packages', {}).get(provider, {}).get('path', 'packages/document-basic')
            if not (basic / name).is_file():
                raise RuntimeError('Unselected product provider: ' + name)
    owners = {p: 'product' for p in selected}
    shared = {name: path for name, path in source_files(root).items() if name.startswith('kits/')}
    if provider == 'tiku-basic':
        package = root / metadata.get('optional_packages', {}).get(provider, {}).get('path', 'packages/document-basic')
        if not (package / 'kits/document/tiku_richdoc.h').is_file():
            raise RuntimeError('Missing basic document provider package')
        shared.update({str(path.relative_to(package)): path for path in (package / 'kits').rglob('*')
                       if path.is_file()})
    for name in list(selected):
        kit = file_kit(name)
        if kit in {'document', 'document_formats'} and provider in {'none', 'tiku-basic'}:
            del selected[name]
            owners.pop(name)
        elif kit in {'deck', 'deck_formats'} and presentation == 'none':
            del selected[name]
            owners.pop(name)
    for name, path in shared.items():
        selected[name] = path
        owners[name] = 'foundation-basic' if 'packages' in path.relative_to(root).parts else 'foundation'
    if provider == 'kintsugi-advanced' and not (product / 'kits/document/tiku_richdoc.h').is_file():
        raise RuntimeError('Missing advanced document provider')
    for name in profile.get('overrides', []):
        if name not in selected or not (product / name).is_file():
            raise RuntimeError('Missing product override: ' + name)
        if file_kit(name) == 'document' or name in shared and 'packages' in shared[name].relative_to(root).parts:
            raise RuntimeError('Conflicting document provider override: ' + name)
        selected[name] = product / name
        owners[name] = 'product-override'
    if profile.get('office_shapes'):
        name = 'kits/office_shapes/tiku_office_dml_geom.c'
        selected[name] = product / profile['office_shapes']
        owners[name] = 'product'
    for name in profile.get('excluded', []):
        selected.pop(name, None)
        owners.pop(name, None)
    wanted = set(selected)
    for old in (output / 'kits').rglob('*'):
        if old.is_symlink() and str(old.relative_to(output)) not in wanted and str(old.relative_to(output)) not in {'kits/translation/tiku_vector_raster.c', 'kits/translation/tiku_vector_raster.h'}:
            old.unlink()
    for name, source in selected.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        relative = os.path.relpath(source, target.parent)
        if target.is_symlink():
            if os.readlink(target) == relative:
                continue
            target.unlink()
        elif target.exists():
            raise RuntimeError('Foundation view contains a non-link source: ' + str(target))
        target.symlink_to(relative)
    enabled = host_files(platform.system(), host)
    lines = []
    for kit in LINK_ORDER:
        sources = sorted(name for name in selected if file_kit(name) == kit
                         and Path(name).suffix in {'.c', '.m'} and 'tools' not in Path(name).parts
                         and (Path(name).name not in HOST_FILES or Path(name).name in enabled))
        lines.append(kit.upper() + ' := ' + ' '.join(sources) + '\n')
    # The moved rasterizer keeps source-including fixture paths valid.
    alias = output / 'kits/translation/tiku_vector_raster.c'
    if not alias.exists():
        alias.symlink_to('../interface/tiku_vector_raster.c')
    alias = output / 'kits/translation/tiku_vector_raster.h'
    if not alias.exists():
        alias.symlink_to('../interface/tiku_vector_raster.h')
    provider_files = {name: path for name, path in selected.items()
                      if owners[name] != 'foundation' and path.suffix in {'.h', '.c', '.m', '.inl'}}
    provider_hash = digest_files(provider_files)
    identity_data = dict(foundation_id=metadata['id'], foundation_revision=actual,
                         document_provider=provider, presentation_provider=presentation, provider_sha256=provider_hash,
                         system=platform.system(), machine=platform.machine(), host=host, abi=5,
                         flags=flags)
    identity = hashlib.sha256(json.dumps(identity_data, sort_keys=True).encode()).hexdigest()
    identity_data.update(sdk_identity=identity, pinned_revision=lock['source_sha256'],
                         development=actual != lock['source_sha256'], source_root=str(root),
                         source_revision=source_revision(root))
    write_changed(output / 'identity.json', (json.dumps(identity_data, indent=2) + '\n').encode())
    write_changed(output / 'tiku_sdk_identity.h', identity_header(identity_data))
    write_changed(output / 'sources.mk', ''.join(lines).encode())
    inventory = {name: dict(owner=owners[name], kit=file_kit(name), source=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                 for name, path in sorted(selected.items()) if path.suffix in {'.c', '.h', '.inl', '.m'}}
    write_changed(output / 'sources.json', (json.dumps(inventory, indent=2) + '\n').encode())
    resources = {name: dict(owner=owners[name], source=str(path)) for name, path in sorted(selected.items())
                 if path.suffix not in {'.c', '.h', '.inl', '.m'} and name != 'kits/Makefile'}
    write_changed(output / 'resources.json', (json.dumps(resources, indent=2) + '\n').encode())
    composition = hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest()
    write_changed(output / 'config.stamp', (identity + '\n' + composition + '\n' + str(root) + '\n' + str(product) + '\n').encode())
    from shell_packages import compose_shell
    compose_shell(root, product, output, profile)
    from application_packages import compose_applications
    compose_applications(root, product, output, profile)
    from check_dependencies import check_migrated_copies
    duplicates = check_migrated_copies(product, output)
    if duplicates:
        raise RuntimeError('\n'.join(duplicates))
    return output / 'sources.mk'


def main():
    """Print a content revision or prepare a product composition."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--hash', action='store_true')
    parser.add_argument('--product', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--host', default='x11')
    parser.add_argument('--flags', default='')
    parser.add_argument('--allow-dirty', action='store_true')
    args = parser.parse_args()
    if args.hash:
        print(digest_files(source_files(args.root.resolve())))
    else:
        print(compose(args.root.resolve(), args.product.resolve(), args.output,
                      args.host, args.flags, args.allow_dirty))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
