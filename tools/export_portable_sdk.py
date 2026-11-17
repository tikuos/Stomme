#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Export source-bound portable archives, selected headers and target contracts."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from export_sdk import publish
from portable_sdk import LIBRARIES, archive_identities, validate

ROOT = Path(__file__).resolve().parents[1]


def consumer_header(data):
    """Require portable types and the selected machine ABI at compilation."""
    checks = ('#if !defined(__riscv) || __riscv_xlen != 32 || !defined(__riscv_float_abi_single)\n'
              '#error "Portable SDK requires rv32 ilp32f"\n#endif\n') if data['target'] == 's31' else (
              '#ifdef __riscv\n#error "Portable SDK requires its host compiler target"\n#endif\n')
    for key, value in data['abi_macros'].items():
        checks += ('#if defined(' + key + ')\n' if value is None else
                   '#if !defined(' + key + ') || ' + key + ' != ' + value + '\n')
        checks += '#error "Portable compiler ABI differs: ' + key + '"\n#endif\n'
    return ('/* Generated portable SDK contract. */\n#ifndef TIKU_PORTABLE_IDENTITY_H_\n'
            '#define TIKU_PORTABLE_IDENTITY_H_\n'
            '#ifndef TIKU_PORTABLE_UI\n#error "Select TIKU_PORTABLE_UI for this SDK"\n#endif\n'
            '#ifdef TIKU_DISPLAYLESS\n#error "This SDK supplies local painting headers"\n#endif\n' + checks +
            '#include "tiku_resource.h"\n'
            '_Static_assert(sizeof(tiku_resource_error_t) == 4, "Portable enum ABI differs");\n'
            'struct tiku_portable_abi_probe { char byte; double number; };\n'
            '_Static_assert(offsetof(struct tiku_portable_abi_probe, number) == _Alignof(double), "Portable structure packing differs");\n'
            '#define TIKU_PORTABLE_SDK_ID "' + data['sdk_identity'] + '"\n'
            '#if !__has_attribute(retain)\n#error "Portable SDK requires retained link guards"\n#endif\n'
            'extern const unsigned char tiku_portable_sdk_' + data['sdk_identity'] + ';\n'
            'static const void *const tiku_portable_link_guard __attribute__((used, retain)) = &tiku_portable_sdk_' +
            data['sdk_identity'] + ';\n#endif\n')


def export(build, output, ar, nm):
    """Pair current sources and archive member identities before publishing."""
    data = json.loads((build / 'profile.json').read_text())
    for name, expected in data['source_sha256'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise ValueError('Rebuild after source change: ' + name)
    expected = {
        'libtiku_models.a': [Path(p).stem + '.o' for p in data['sources']] + ['identity.o'],
        'libtiku_render.a': [Path(p).stem + '.o' for p in data['render_sources']],
        'libtiku_font_subset.a': ['tiku_font_subset.o', 'tiku_font_subset_data.o']}
    inventories = {name: archive_identities(build / 'lib' / name, ar, data['sdk_identity'], expected[name]) for name in LIBRARIES}
    output = output.absolute()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.portable-sdk-', dir=output.parent) as temp:
        stage = Path(temp) / 'sdk'
        for directory in ['include', 'lib', 'tools', 'licenses']:
            (stage / directory).mkdir(parents=True)
        headers = {}
        for name in data['headers']:
            source = ROOT / name
            if source.name in headers:
                raise ValueError('Duplicate exported header: ' + source.name)
            headers[source.name] = name
            shutil.copy2(source, stage / 'include' / source.name)
        for name in LIBRARIES:
            shutil.copy2(build / 'lib' / name, stage / 'lib' / name)
        for name in ['LICENSE', 'NOTICE']:
            shutil.copy2(ROOT / name, stage / 'licenses' / name)
        shutil.copy2(build / 'generated/LICENSE.DejaVu', stage / 'licenses/LICENSE.DejaVu')
        shutil.copy2(ROOT / 'tools/portable_sdk.py', stage / 'tools/portable_sdk.py')
        (stage / 'include/tiku_portable_identity.h').write_text(consumer_header(data))
        flags = ['-std=c11', '-DTIKU_PORTABLE_UI=1']
        if data['target'] == 's31':
            flags += ['-march=' + data['isa'], '-mabi=' + data['abi']]
        exports = set()
        for name in LIBRARIES:
            text = subprocess.check_output([nm, '-g', '--defined-only', str(build / 'lib' / name)], text=True)
            exports.update(line.split()[-1] for line in text.splitlines() if len(line.split()) >= 3 and line.split()[-1].startswith('tiku_'))
        config = ['# Portable SDK; validate before selecting archives.\n',
            'ifndef SDK_ROOT\nSDK_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))\nendif\n',
            'SDK_TARGET := ' + data['target'] + '\n',
            'SDK_ABI := ' + data['abi'] + '\n',
            'SDK_IDENTITY := ' + data['sdk_identity'] + '\n',
            'SDK_CAPABILITIES := ' + ' '.join(data['capabilities']) + '\n',
            'SDK_REQUIRED_CAPABILITIES ?=\n',
            'SDK_KEEP_SYMBOLS := ' + ' '.join('-Wl,-u,' + name for name in sorted(exports)) + '\n',
            'SDK_CHECK := $(shell python3 "$(SDK_ROOT)/tools/portable_sdk.py" --sdk "$(SDK_ROOT)" --target "$(FOUNDATION_TARGET)" --cc "$(CC)" --require $(SDK_REQUIRED_CAPABILITIES))\n',
            'ifneq ($(SDK_CHECK),ok)\n$(error Incompatible or damaged portable SDK)\nendif\n',
            'SDK_CFLAGS := ' + ' '.join(flags) + ' -I"$(SDK_ROOT)/include" -include "$(SDK_ROOT)/include/tiku_portable_identity.h"\n',
            'SDK_LIBRARIES := -Wl,--start-group ' + ' '.join('"$(SDK_ROOT)/lib/' + name + '"' for name in LIBRARIES) + ' -Wl,--end-group\n']
        (stage / 'sdk.mk').write_text(''.join(config))
        metadata = dict(data, libraries=LIBRARIES, members=inventories,
                        public_symbols=sorted(exports), consumer_flags=flags, header_sources=headers)
        metadata['files_sha256'] = {str(p.relative_to(stage)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(stage.rglob('*')) if p.is_file()}
        (stage / 'sdk.json').write_text(json.dumps(metadata, sort_keys=True, indent=2) + '\n')
        validate(stage, data['target'])
        publish(stage, output, data['foundation_id'])
    print('Portable SDK:', output)


def main():
    """Read explicit target build artifacts and export a matching SDK."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ar', default='ar')
    parser.add_argument('--nm', default='nm')
    args = parser.parse_args()
    export(args.build, args.output, args.ar, args.nm)


if __name__ == '__main__':
    main()
