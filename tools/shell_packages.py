#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compose shared shell sources with product identity and optional services."""
from pathlib import Path
import hashlib
import json
import os
import re


def selection(root, product, profile):
    """Validate the selected shell and its explicit product service flags."""
    name = profile.get('shell')
    if name is None:
        return None
    package = root / 'packages/shell'
    if name != 'desktop' or not (package / 'package.json').is_file():
        raise RuntimeError('Unknown shell package selection')
    config = product / 'system/product/tiku_shell_product.h'
    if not config.is_file():
        raise RuntimeError('Missing product shell configuration')
    text = config.read_text()
    for flag in ('BOARD_SERVICES', 'REMOTE_SERVICES', 'NOTE_TABS'):
        if not re.search(r'^#define TIKU_SHELL_' + flag + r' [01]$', text, re.M):
            raise RuntimeError('Invalid shell service selection: ' + flag)
    return package


def selected_tests(root, product, profile):
    """Return the shell regression sources for the selected product profile."""
    package = selection(root, product, profile)
    if package is None:
        return {}
    return {name: package / name for name in json.loads((package / 'package.json').read_text())['tests']}


def compose_shell(root, product, output, profile):
    """Link shared runtime sources and product extensions into an owned view."""
    from application_packages import tree_files
    from product_bridge import write_changed
    selected = tree_files(product, 'system')
    owners = {name: 'product' for name in selected}
    package = selection(root, product, profile)
    if package:
        for name in json.loads((package / 'package.json').read_text())['sources']:
            selected[name] = package / name
            owners[name] = 'foundation-shell'
    for path in (output / 'system').rglob('*'):
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
            raise RuntimeError('Shell view contains a non-link source: ' + str(target))
        if not target.is_symlink():
            target.symlink_to(relative)
        inventory[name] = dict(owner=owners[name], source=str(source),
                               sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    write_changed(output / 'shell-sources.json', (json.dumps(inventory, indent=2) + '\n').encode())
