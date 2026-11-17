#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Build relocated portable consumers using exported files and local providers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import check_portable
from portable_sdk import validate

EXAMPLES = ['examples/portable-sdk/consumer.c', 'examples/portable-panel/panel.c',
            'examples/portable-panel/panel.h', 'examples/portable-browser/browser.c',
            'examples/portable-browser/browser.h', 'examples/portable-calculator/calculator.c',
            'examples/portable-calculator/calculator.h', 'platform/host-test/tiku_test_pool.c',
            'platform/host-test/tiku_test_pool.h', 'platform/host-test/tiku_memory_presenter.c',
            'platform/host-test/tiku_memory_presenter.h']


def run(sdk, output, cc, nm):
    """Copy the SDK and example, then build without source-tree include access."""
    data = validate(sdk, cc=cc, required=['local-rendering', 'static-applications', 'paged-storage-read'])
    if output.is_symlink() or (output.exists() and not (output / '.portable-consumer').exists()):
        raise ValueError('Consumer output is not owned by this runner')
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / '.portable-consumer').write_text('1\n')
    relocated = output / 'relocated sdk'
    shutil.copytree(sdk, relocated)
    consumer = output / 'consumer'
    consumer.mkdir()
    for name in EXAMPLES:
        shutil.copy2(ROOT / name, consumer / Path(name).name)
    make = '''SDK_REQUIRED_CAPABILITIES := local-rendering static-applications paged-storage-read word-calculator
include ../relocated\\ sdk/sdk.mk
SOURCES := consumer.c panel.c browser.c calculator.c tiku_test_pool.c tiku_memory_presenter.c
ifeq ($(SDK_LINK_MODE),closure)
LINK_LIBS := $(SDK_KEEP_SYMBOLS) -Wl,--whole-archive $(SDK_LIBRARIES) -Wl,--no-whole-archive
else
LINK_LIBS := $(SDK_LIBRARIES)
LINK_GC := -Wl,--gc-sections
endif
ifeq ($(FOUNDATION_TARGET),s31)
TARGET_CFLAGS := -DTIKU_SDK_LINK_ONLY=1
TARGET_LDFLAGS := -nostdlib -static -Wl,--no-undefined,-e,tiku_sdk_probe
RUNTIME_LIBS := -Wl,--start-group -lc -lgcc -Wl,--end-group
endif
consumer.elf: $(SOURCES)
	$(CC) -O2 -g -ffunction-sections -fdata-sections $(SDK_CFLAGS) $(TARGET_CFLAGS) -I. $(SOURCES) $(TARGET_LDFLAGS) $(LINK_GC) $(LINK_LIBS) $(RUNTIME_LIBS) -Wl,-Map,$@.map -o $@
'''
    (consumer / 'Makefile').write_text(make)
    reports = {}
    for mode in ['selected', 'closure']:
        command = ['make', '-B', 'CC=' + cc, 'FOUNDATION_TARGET=' + data['target'], 'SDK_LINK_MODE=' + mode, 'SDK_ROOT=' + str(relocated)]
        result = subprocess.run(command, cwd=consumer, capture_output=True, text=True)
        (output / (mode + '-build.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Consumer build failed: ' + result.stdout + result.stderr)
        binary = output / (mode + '.elf')
        shutil.copy2(consumer / 'consumer.elf', binary)
        shutil.copy2(consumer / 'consumer.elf.map', output / (mode + '.map'))
        report = check_portable.check(nm, [binary], data, linked=True)
        if report['errors']:
            raise RuntimeError('\n'.join(report['errors']))
        if data['target'] == 'host-test':
            subprocess.run([str(binary)], cwd=consumer, check=True)
        size_tool = nm.removesuffix('nm') + 'size'
        sections = subprocess.check_output([size_tool, '-A', str(binary)], text=True)
        (output / (mode + '-size.txt')).write_text(sections)
        sizes = {fields[0]: int(fields[1]) for line in sections.splitlines()
                 if len(fields := line.split()) >= 2 and fields[0].startswith('.') and fields[1].isdigit()}
        symbols = check_portable.symbols(nm, binary)[0]
        if mode == 'closure' and set(data['public_symbols']) - symbols:
            raise RuntimeError('Incomplete whole-profile symbol closure')
        report['arithmetic_helpers'] = sorted(s for s in symbols if re.fullmatch(r'__(?:.*df.*|.*di3|.*idf)', s))
        report['sections'] = sizes
        report['sdk_identity'] = data['sdk_identity']
        reports[mode] = report
    if data['target'] == 'host-test':
        source = consumer / 'painting.c'
        shutil.copy2(ROOT / 'tests/portable/painting.c', source)
        binary = output / 'painting'
        command = shlex.split(cc) + ['-O2', *data['consumer_flags'], '-I' + str(relocated / 'include'),
                  '-include', str(relocated / 'include/tiku_portable_identity.h'), str(source),
                  '-Wl,--start-group', *[str(relocated / 'lib' / name) for name in data['libraries']],
                  '-Wl,--end-group', '-o', str(binary)]
        subprocess.run(command, cwd=consumer, check=True)
        pixels = subprocess.check_output([str(binary)], cwd=consumer)
        fixture = json.loads((ROOT / 'tests/portable/painting.json').read_text())
        import check_pixels
        if not check_pixels.check(pixels, fixture):
            raise RuntimeError('Relocated renderer differs from desktop pixels')
        reports['pixel_sha256'] = hashlib.sha256(pixels).hexdigest()
    (output / 'report.json').write_text(json.dumps(reports, indent=2) + '\n')
    print('Relocated portable SDK consumers passed:', data['target'])
    return reports


def main():
    """Select the SDK, toolchain and isolated consumer output directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--nm', default='nm')
    args = parser.parse_args()
    run(args.sdk.resolve(), args.output.resolve(), args.cc, args.nm)


if __name__ == '__main__':
    main()
