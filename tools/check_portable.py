#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check selected objects and complete linked probes for undeclared dependencies."""
import argparse
import json
from pathlib import Path
import re
import subprocess

HEAP = re.compile(r'^(?:_?malloc|_?calloc|_?realloc|_?free|strdup|strndup|aligned_alloc|posix_memalign|_sbrk|sbrk|heap_caps_.*|pvPortMalloc|vPortFree)(?:_r)?$')
NATIVE = re.compile(r'^(?:pthread_.*|dlopen|dlsym|dlclose|fork|exec.*|system|popen|socket|connect|X[A-Z].*|g_bus_.*|g_dbus_.*|objc_.*)$')


def symbols(nm, path):
    """Return defined and undefined names, without dynamic symbol versions."""
    text = subprocess.check_output([nm, str(path)], text=True)
    defined, undefined = set(), set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and len(fields[-2]) == 1:
            kind, name = fields[-2], fields[-1].split('@')[0]
            if kind in {'U', 'w', 'v'}:
                undefined.add(name)
            elif kind.upper() in {'T', 'D', 'B', 'R', 'S', 'G', 'W', 'V', 'A'}:
                defined.add(name)
    return defined, undefined


def check(nm, paths, profile, linked=False):
    """Reject heap/native symbols and undeclared unresolved object references."""
    defined, undefined, inventory, errors = set(), set(), {}, []
    for path in paths:
        defs, refs = symbols(nm, path)
        inventory[str(path)] = dict(defined=sorted(defs), undefined=sorted(refs))
        defined |= defs
        undefined |= refs
        for name in defs | refs:
            if re.fullmatch(r'(?:f[12]_)?(?:plain|bold|mono).*_(?:bits|glyphs|font)', name) and name not in {'plain_bits', 'plain_glyphs', 'bold_bits', 'bold_glyphs'}:
                errors.append(str(path) + ': unselected font ' + name)
            normalized = name[1:] if name.startswith('_') and not name.startswith('__') else name
            if HEAP.fullmatch(name) or HEAP.fullmatch(normalized) or NATIVE.fullmatch(normalized):
                errors.append(str(path) + ': forbidden dependency ' + name)
    support = set()
    for path, item in inventory.items():
        if '/kits/support/' in '/' + path:
            support.update(item['defined'])
    allocation_api = {'tiku_buffer_init', 'tiku_buffer_borrow',
                      'tiku_buffer_resize', 'tiku_buffer_dispose'}
    for path, item in inventory.items():
        if '/kits/interface/' in '/' + path or path.endswith('/tiku_allocator.o'):
            errors += [path + ': forbidden support symbol ' + name
                       for name in sorted((set(item['undefined']) & support) - allocation_api)]
    unresolved = undefined - defined
    if linked and profile['target'] == 's31' and unresolved:
        errors.append('Unresolved freestanding probe symbols: ' + ', '.join(sorted(unresolved)))
    if not linked:
        allowed = set(profile['runtime']) | set(profile['providers'])
        # Compiler-emitted arithmetic for the recorded S31 ABI.
        if profile['target'] == 's31':
            allowed |= {'__udivdi3', '__umoddi3', '__divdi3', '__adddf3', '__subdf3', '__muldf3', '__divdf3', '__floatsidf', '__floatdidf', '__fixdfsi', '__floatundidf', '__ashldi3', '__lshrdi3', '__floatdisf'}
        errors += ['Undeclared dependency: ' + name for name in sorted(unresolved - allowed)]
    return dict(objects=inventory, unresolved=sorted(unresolved), errors=errors)


def includes(root, sources):
    """Check the source/header closure against the portable C header allowlist."""
    standard = {'stddef.h', 'stdint.h', 'limits.h', 'string.h', 'stdio.h',
                'stdlib.h', 'math.h', 'stdbool.h', 'float.h'}
    pending = [root / source for source in sources]
    seen, errors = set(), []
    while pending:
        path = pending.pop().resolve()
        if path in seen:
            continue
        seen.add(path)
        if not (path.is_relative_to(root.resolve() / 'kits/interface') or
                path.is_relative_to(root.resolve() / 'kits/application/portable') or
                path.is_relative_to(root.resolve() / 'kits/storage/portable') or
                path.is_relative_to(root.resolve() / 'packages/apps/models') or
                path in {root.resolve() / name for name in [
                    'kits/support/tiku_resource.h', 'kits/support/tiku_msg.h',
                    'kits/storage/tiku_resource_registry.h',
                    'packages/apps/apps/developer-calculator/tiku_calculator_model.inl']} or
                path in {root.resolve() / 'kits/support/portable' / name for name in
                         ['tiku_allocator.c', 'tiku_allocator.h', 'tiku_clock.c', 'tiku_clock.h']}):
            errors.append('External portable include: ' + str(path))
            continue
        for delimiter, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', path.read_text(), re.M):
            if path.name == 'tiku_allocator.h' and name not in {'stddef.h', 'stdint.h'}:
                errors.append(str(path) + ': allocator contract includes ' + name)
            if delimiter == '<' and name in standard:
                continue
            candidates = [path.parent / name, root / 'kits/interface' / name, root / 'kits/support/portable' / name] + [root / directory / name for directory in [
                'kits/interface/portable', 'kits/support', 'kits/storage', 'kits/application/portable',
                'kits/storage/portable', 'packages/apps/models', 'packages/apps/apps/developer-calculator']]
            included = next((p for p in candidates if p.is_file()), None)
            if (path.is_relative_to(root.resolve() / 'kits/interface') and
                    included is not None and included.resolve().is_relative_to(root.resolve() / 'kits/support') and
                    name != 'tiku_allocator.h'):
                errors.append(str(path) + ': forbidden portable support contract ' + name)
            if included is None or name in {'tiku_host.h', 'tiku_ttf.h', 'tiku_cff.h', 'tiku_type1.h'}:
                errors.append(str(path) + ': forbidden portable include ' + name)
            else:
                pending.append(included)
    return errors


def main():
    """Inspect the selected artifacts and save their dependency inventory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--profile', type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--objects', type=Path, nargs='+')
    group.add_argument('--linked', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text())
    report = check(args.nm, args.objects or [args.linked], profile, bool(args.linked))
    report['errors'] += includes(Path(__file__).resolve().parents[1], profile['sources'] + profile.get('render_sources', []) + ['kits/interface/portable/tiku_font_subset.c'])
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    if report['errors']:
        raise SystemExit('\n'.join(report['errors']))
    print('Portable dependency check passed')


if __name__ == '__main__':
    main()
