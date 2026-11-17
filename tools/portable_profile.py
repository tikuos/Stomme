#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Select bounded control sources and record explicit compiler target identity."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
from build_config import configure


INCLUDE_DIRS = ['kits/interface', 'kits/interface/portable', 'kits/support',
                'kits/support/portable', 'kits/storage', 'kits/storage/portable',
                'kits/application/portable', 'packages/apps/models',
                'packages/apps/apps/developer-calculator']


def source_closure(root, sources):
    """Resolve quoted includes for selected sources, preserving relative names."""
    pending, seen = [root / name for name in sources], set()
    while pending:
        path = pending.pop().resolve()
        if path in seen:
            continue
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            raise ValueError('Invalid portable dependency: ' + str(path))
        seen.add(path)
        for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', path.read_text(), re.M):
            choices = [path.parent / name] + [root / directory / name for directory in INCLUDE_DIRS]
            found = next((p for p in choices if p.is_file()), None)
            if found is None:
                raise ValueError('Missing portable dependency: ' + name)
            pending.append(found)
    return sorted(str(p.relative_to(root.resolve())) for p in seen)


def identity_header(data):
    """Embed nonallocated object provenance, including in fat LTO objects."""
    assembly = '.pushsection .tiku.identity,""\n.asciz "TIKU_PORTABLE_ID=' + data['sdk_identity'] + '"\n.popsection\n'
    return ('/* Generated portable object identity. */\n'
            '#ifndef TIKU_PORTABLE_BUILD_IDENTITY_H_\n#define TIKU_PORTABLE_BUILD_IDENTITY_H_\n'
            '__asm__(' + json.dumps(assembly) + ');\n#endif\n')



def select(root, target, compiler):
    """Validate the target/compiler pair and return the source selection."""
    if target not in {'host-test', 's31'}:
        raise ValueError('Unsupported portable target: ' + target)
    triple = subprocess.check_output(shlex.split(compiler) + ['-dumpmachine'], text=True).strip()
    if target == 's31' and not triple.startswith('riscv32-'):
        raise ValueError('s31 requires a riscv32 compiler, got ' + triple)
    if target == 'host-test' and 'linux' not in triple:
        raise ValueError('host-test requires a Linux compiler, got ' + triple)
    profile = json.loads((root / 'profiles/portable-ui.json').read_text())
    for name in profile['sources'] + profile.get('render_sources', []):
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or not (root / path).is_file():
            raise ValueError('Invalid portable source: ' + name)
    return dict(format=1, foundation_id='desktop-foundation', profile=profile['profile'],
                target=target, compiler=compiler, compiler_target=triple,
                compiler_version=subprocess.check_output(shlex.split(compiler) + ['--version'], text=True).splitlines()[0],
                abi='ilp32f' if target == 's31' else triple,
                isa='rv32imafc_zicsr_zifencei' if target == 's31' else triple,
                capabilities=profile['capabilities'], sources=profile['sources'], render_sources=profile.get('render_sources', []), requires=profile['requires'],
                fonts='profiles/portable-fonts.json', runtime=profile['runtime'],
                providers=profile['providers'],
                profile_sha256=hashlib.sha256((root / 'profiles/portable-ui.json').read_bytes()).hexdigest())


def main():
    """Emit Make input and target metadata into an owned output directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--flags', required=True)
    parser.add_argument('--tools', default='')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    data = select(root, args.target, args.cc)
    data['flags'] = args.flags
    data['tools'] = args.tools
    macros = subprocess.check_output(shlex.split(args.cc) + shlex.split(args.flags) +
        ['-dM', '-E', '-x', 'c', '-'], input='', text=True)
    defines = dict(re.findall(r'^#define (\w+) (.+)$', macros, re.M))
    keys = ['__SIZEOF_POINTER__', '__SIZEOF_INT__', '__SIZEOF_LONG__',
            '__SIZEOF_SIZE_T__', '__SIZEOF_FLOAT__', '__SIZEOF_DOUBLE__',
            '__BYTE_ORDER__', '__riscv_xlen', '__riscv_float_abi_single',
            '__riscv_float_abi_double', '__CHAR_UNSIGNED__']
    data['abi_macros'] = {key: defines.get(key) for key in keys}
    if args.target == 'host-test':
        native = subprocess.check_output(shlex.split(args.cc) + ['-dM', '-E', '-x', 'c', '-'], input='', text=True)
        native_defines = dict(re.findall(r'^#define (\w+) (.+)$', native, re.M))
        if any(defines.get(key) != native_defines.get(key) for key in keys):
            raise ValueError('host-test requires the compiler default machine ABI')
    if args.target == 's31' and (defines.get('__riscv_xlen') != '32' or
            defines.get('__riscv_float_abi_single') != '1'):
        raise ValueError('s31 requires rv32 ilp32f compiler flags')
    if any(flag.startswith(('-fshort-enums', '-fpack-struct')) for flag in shlex.split(args.flags)):
        raise ValueError('Portable SDK requires default enum and structure layout')
    selected = data['sources'] + data['render_sources'] + ['kits/interface/portable/tiku_font_subset.c']
    closure = source_closure(root, selected)
    inputs = sorted(set(closure + ['profiles/portable-ui.json', 'profiles/portable-fonts.json',
        'kits/interface/tiku_font_data.h', 'mk/portable.mk', 'tools/portable_profile.py',
        'tools/subset_fonts.py', 'tools/check_portable.py', 'tools/portable_sdk.py',
        'tools/export_portable_sdk.py']))
    data['source_sha256'] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in inputs}
    data['headers'] = [name for name in closure if name.endswith('.h')]
    data['sdk_identity'] = hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()
    encoded = json.dumps(data, sort_keys=True, indent=2) + '\n'
    configure(args.output, 'portable-ui/' + args.target, encoded)
    for name, content in [('portable_identity.c', '/* Generated SDK link identity. */\nconst unsigned char tiku_portable_sdk_' + data['sdk_identity'] + ' = 0;\n'), ('portable_build_identity.h', identity_header(data)), ('profile.json', encoded), ('profile.mk', 'PORTABLE_SOURCES := ' + ' '.join(data['sources']) + '\nPORTABLE_RENDER_SOURCES := ' + ' '.join(data['render_sources']) + '\n')]:
        path = args.output / name
        if not path.exists() or path.read_text() != content:
            path.write_text(content)
    print(args.output / 'profile.mk')


if __name__ == '__main__':
    main()
