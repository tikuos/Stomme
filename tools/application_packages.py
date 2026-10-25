#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compose shared application implementations with product manifests and defaults."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys


def tree_files(root, subtree):
    """Read source and fixture files while excluding generated application builds."""
    files = {}
    for directory, dirs, names in os.walk(root / subtree):
        dirs[:] = [name for name in dirs if name not in {'build', '__pycache__', '.git'}]
        for name in names:
            path = Path(directory) / name
            if path.is_file():
                files[str(path.relative_to(root))] = path
    return files


def compose_applications(root, product, output, profile):
    """Link selected package sources and product data into one owned application view."""
    package = root / 'packages/apps'
    requested = profile.get('applications', [])
    metadata = json.loads((package / 'package.json').read_text()) if package.is_dir() else {}
    allowed = set(metadata.get('applications', [])) | ({'editor-common'} if package.is_dir() else set())
    if not isinstance(requested, list) or any(not isinstance(name, str) or name not in allowed for name in requested):
        raise RuntimeError('Unknown application package selection')
    for name in requested:
        missing = set(metadata.get('requires', {}).get(name, [])) - set(requested)
        if missing:
            raise RuntimeError('Missing application dependency for ' + name + ': ' + ', '.join(sorted(missing)))
    if not (product / 'apps').is_dir():
        requested = []
    if requested and not (product / 'apps/shared/tiku_app_product.h').is_file():
        raise RuntimeError('Shared applications require product configuration')
    selected = tree_files(product, 'apps') | tree_files(product, 'tests')
    owners = {name: 'product' for name in selected}
    shared = tree_files(package, 'apps') | tree_files(package, 'tests')
    for name, source in shared.items():
        parts = Path(name).parts
        enabled = parts[0] == 'apps' and (parts[1] in requested or bool(requested) and name in metadata.get('common', []) or name in metadata.get('editor_common', []) and 'editor-common' in requested)
        if parts[0] == 'tests':
            enabled = bool(requested) and name in metadata.get('tests', [])
        if enabled:
            selected[name] = source
            owners[name] = 'foundation-apps'
    kit_tests = tree_files(root, 'tests/kits') | tree_files(root, 'tests/fixtures')
    if profile.get('document_provider') == 'tiku-basic':
        basic = root / 'packages/document-basic'
        kit_tests.update({'tests/kits/' + path.name: path
                          for path in (basic / 'tests').glob('test_*.c')})
    for name, source in kit_tests.items():
        if name not in selected or owners[name] == 'product':
            selected[name] = source
            owners[name] = 'foundation-tests'
    from shell_packages import selected_tests
    for name, source in selected_tests(root, product, profile).items():
        selected[name] = source
        owners[name] = 'foundation-shell'
    output.mkdir(parents=True, exist_ok=True)
    marker = output / '.foundation-applications'
    if not marker.exists() and not (output / '.foundation-view').exists() and any(output.iterdir()):
        raise RuntimeError('Application view contains unrecognized output')
    marker.touch(exist_ok=True)
    for subtree in ['apps', 'tests']:
        for path in (output / subtree).rglob('*'):
            if path.is_symlink() and str(path.relative_to(output)) not in selected:
                path.unlink()
    inventory = {}
    for name, source in sorted(selected.items()):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        relative = os.path.relpath(source, target.parent)
        if target.is_symlink():
            if os.readlink(target) != relative:
                target.unlink()
        elif target.exists():
            raise RuntimeError('Application view contains a non-link source: ' + str(target))
        if not target.is_symlink():
            target.symlink_to(relative)
        inventory[name] = dict(owner=owners[name], source=str(source), sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    from product_bridge import write_changed
    write_changed(output / 'application-sources.json', (json.dumps(inventory, indent=2) + '\n').encode())
    return output / 'apps'


def main():
    """Prepare independent application sources without building desktop binaries."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--product', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--app-build', type=Path)
    args = parser.parse_args()
    root, product = args.root.resolve(), args.product.resolve()
    from product_bridge import digest_files, source_files
    lock = json.loads((product / 'foundation.lock.json').read_text())
    if digest_files(source_files(root)) != lock['source_sha256']:
        raise RuntimeError('Foundation source differs from foundation.lock.json')
    if args.app_build:
        subprocess.run([sys.executable, str(product / 'apps/package_app.py'), 'prepare', str(args.app_build)], check=True)
    result = compose_applications(root, product, args.output, json.loads((product / 'foundation.product.json').read_text()))
    from check_dependencies import check_migrated_copies
    duplicates = check_migrated_copies(product, args.output)
    if duplicates:
        raise RuntimeError('\n'.join(duplicates))
    print(result)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
