#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check kit includes and displayless model boundaries without product source."""
from pathlib import Path
import argparse
import re
import json
from kit_packages import file_kit

ROOT = Path(__file__).resolve().parents[1]
ALLOWS = {'support': set(), 'device': {'support'}, 'interface': set(),
          'storage': {'support', 'device'}, 'translation': {'support', 'storage', 'interface'},
          'application': {'support', 'device', 'interface'}, 'media': set(),
          'editor': {'support', 'interface'},
          'document': {'support', 'interface'},
          'document_formats': {'document', 'translation', 'support', 'storage', 'interface'},
          'deck': {'document', 'support', 'interface'},
          'deck_formats': {'office_shapes', 'deck', 'document', 'document_formats', 'translation', 'support', 'storage', 'interface'},
          'office_shapes': {'deck', 'translation', 'support', 'interface'}}
INCLUDES = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
GENERATED = {'tiku_strings_default.h', 'tiku_saslprep_tables.h', 'tiku_scores_default.h', 'tiku_hyphen_en_us.h'}
BEHAVIOR = {'tiku_field', 'tiku_scroll', 'tiku_textview', 'tiku_slider', 'tiku_doc',
            'tiku_markdown', 'tiku_field_event', 'tiku_dialog', 'tiku_press', 'tiku_choice',
            'tiku_input', 'tiku_text_input', 'tiku_sensor', 'tiku_text_layout',
            'tiku_properties', 'tiku_preferences_layout', 'tiku_tabs', 'tiku_list',
            'tiku_layout', 'tiku_colors', 'tiku_measure', 'tiku_geom', 'tiku_event',
            'tiku_form_row'}
PAINT = {'tiku_gfx.h', 'tiku_font.h', 'tiku_ui.h', 'tiku_dl.h', 'tiku_window.h', 'tiku_theme.h'}


def check(root, provider='none'):
    """Return violations of include ownership or public source closure."""
    errors = []
    owners = {}
    files = {str(path.relative_to(root)): path for path in (root / 'kits').rglob('*')
             if path.suffix in {'.c', '.h', '.inl', '.m'} and 'tools' not in path.parts}
    if provider == 'tiku-basic' and (root / 'packages').is_dir():
        package = root / 'packages/document-basic'
        files.update({str(path.relative_to(package)): path for path in (package / 'kits').rglob('*')
                      if path.suffix in {'.c', '.h', '.inl', '.m'}})
    for name, path in files.items():
        if path.suffix in {'.h', '.inl'}:
            if path.name in owners and files[owners[path.name][1]].resolve() != path.resolve():
                errors.append('Duplicate include name: ' + path.name)
            if path.name in owners and files[owners[path.name][1]].resolve() == path.resolve():
                continue
            owners[path.name] = (file_kit(name), name)
    for name, path in sorted(files.items()):
        relative = Path(name)
        kit = file_kit(name)
        if kit not in ALLOWS:
            errors.append(str(relative) + ': unselected kit')
            continue
        if provider == 'none' and kit in {'document', 'document_formats', 'deck', 'deck_formats', 'office_shapes'}:
            errors.append(str(relative) + ': provider source in base configuration')
        model = path.stem in BEHAVIOR or (path.suffix == '.inl' and any(
            path.stem.startswith(stem + '_') for stem in BEHAVIOR))
        text = path.read_text(errors='replace')
        for include in re.findall(r'^\s*#\s*include\s*<([^>]+)>', text, re.M):
            parts = Path(include).parts
            if Path(include).is_absolute() or '..' in parts or any(
                    part in {'apps', 'system', 'board'} for part in parts):
                errors.append(str(relative) + ': product or external include ' + include)
            target = owners.get(include, (None, None))[0]
            if target and target != kit and target not in ALLOWS[kit]:
                errors.append(str(relative) + ': forbidden kit dependency ' + target)
        for include in INCLUDES.findall(text):
            parts = Path(include).parts
            if Path(include).is_absolute() or '..' in parts or any(
                    part in {'apps', 'system', 'board'} for part in parts):
                errors.append(str(relative) + ': product or external include ' + include)
                continue
            target = owners.get(include, (None, None))[0]
            if target is None and include not in GENERATED:
                errors.append(str(relative) + ': missing quoted include ' + include)
            elif target and target != kit and target not in ALLOWS[kit]:
                errors.append(str(relative) + ': forbidden kit dependency ' + target)
            if kit == 'interface' and model and include in PAINT:
                errors.append(str(relative) + ': displayless model includes paint header ' + include)
    return errors


def check_migrated_copies(product, view):
    """Reject product files already owned by a selected foundation package."""
    profile_path = product / 'foundation.product.json'
    profile = json.loads(profile_path.read_text()) if profile_path.is_file() else {}
    retained = set(profile.get('overrides', []))
    if profile.get('office_shapes'):
        retained.add(profile['office_shapes'])
    migrated = {'kits/tools/check_kits.py'}
    for name in ('sources.json', 'application-sources.json', 'shell-sources.json', 'resources.json'):
        inventory = view / name
        if inventory.is_file():
            migrated.update(path for path, item in json.loads(inventory.read_text()).items()
                            if item['owner'].startswith('foundation'))
    for name in ('tiku_vector_raster.c', 'tiku_vector_raster.h'):
        if (view / 'kits/interface' / name).exists():
            migrated.add('kits/translation/' + name)
    return ['Migrated product copy: ' + name for name in sorted(migrated - retained)
            if (product / name).exists() or (product / name).is_symlink()]


def check_product(product, view):
    """Reject private kit headers in apps and document types in shell interfaces."""
    errors = check_migrated_copies(product, view)
    private = {p.name for p in (view / 'kits').glob('*/*_private.h')}
    provider_headers = {p.name for p in (view / 'kits').glob('*/*.h')
                        if file_kit(str(p.relative_to(view))) in {'document', 'document_formats', 'deck', 'deck_formats'}}
    includes = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
    for subtree in ('apps', 'system'):
        scan_root = view if (view / subtree).is_dir() else product
        for path in (scan_root / subtree).rglob('*'):
            if path.suffix not in {'.c', '.h', '.inl', '.m'}:
                continue
            text = path.read_text(errors='replace')
            for name in includes.findall(text):
                if subtree == 'apps' and ('system/' in name or 'tiku_trk_' in name):
                    errors.append(str(path.relative_to(scan_root)) + ': desktop-private application import ' + name)
                if Path(name).name in private:
                    errors.append(str(path.relative_to(scan_root)) + ': private kit header ' + name)
                if subtree == 'system' and Path(name).name in provider_headers:
                    errors.append(str(path.relative_to(scan_root)) + ': document provider in shell interface ' + name)
            if subtree == 'system' and re.search(r'\btiku_(rich|deck)\w*_t\b', text):
                errors.append(str(path.relative_to(scan_root)) + ': document model type in shell interface')
    return errors


def main():
    """Check the source tree supplied by --root or this checkout."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--product', type=Path)
    parser.add_argument('--provider', default='none', choices=['none', 'tiku-basic', 'kintsugi-advanced'])
    args = parser.parse_args()
    identity = args.root / 'identity.json'
    provider = json.loads(identity.read_text())['document_provider'] if identity.is_file() else args.provider
    errors = check(args.root, provider)
    if args.product:
        errors += check_product(args.product, args.root)
    if errors:
        print('\n'.join(errors))
        raise SystemExit(1)
    print('Kit and optional provider include closure and displayless boundaries passed')


if __name__ == '__main__':
    main()
