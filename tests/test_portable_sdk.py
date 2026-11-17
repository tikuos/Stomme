#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise relocatable SDK export and reject incompatible or stale artifacts."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import portable_sdk
import export_portable_sdk


class SDKTests(unittest.TestCase):
    """Build isolated artifacts and exercise their installed contracts."""

    @classmethod
    def setUpClass(cls):
        """Create an SDK independent of repository build output."""
        cls.temp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.temp.name)
        cls.build = cls.out / 'build'
        result = subprocess.run(['make', '-s', '-j4', 'FOUNDATION_PROFILE=portable-ui',
                                 'BUILD=' + str(cls.build), 'check-sdk'], cwd=ROOT,
                                capture_output=True, text=True)
        if result.returncode:
            cls.temp.cleanup()
            raise RuntimeError(result.stdout + result.stderr)
        cls.sdk = cls.build / 'sdk'

    @classmethod
    def tearDownClass(cls):
        """Remove the isolated output tree."""
        cls.temp.cleanup()

    def test_metadata_and_header_abi(self):
        """Require the target, capabilities, header closure and compatible enum layout."""
        data = portable_sdk.validate(self.sdk, 'host-test', required=['paged-storage-read'], cc='cc')
        for kwargs in [dict(target='s31'), dict(abi='ilp32d'), dict(required=['storage-write'])]:
            with self.assertRaises(ValueError):
                portable_sdk.validate(self.sdk, **kwargs)
        source = self.out / 'headers.c'
        source.write_text(''.join('#include "' + p.name + '"\n' for p in sorted((self.sdk / 'include').glob('*.h'))))
        command = ['cc', *data['consumer_flags'], '-I' + str(self.sdk / 'include'),
                   '-include', str(self.sdk / 'include/tiku_portable_identity.h'), '-fsyntax-only', str(source)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(command + ['-fshort-enums'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('enum ABI differs', result.stderr)
        result = subprocess.run(command + ['-fpack-struct=1'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('structure packing differs', result.stderr)
        result = subprocess.run(command + ['-DTIKU_DISPLAYLESS=1'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('local painting', result.stderr)

    def test_mismatched_headers_at_link(self):
        """Retain the revision reference through GC and LTO when a header is stale."""
        data = portable_sdk.validate(self.sdk)
        identity = data['sdk_identity']
        other = ('0' if identity[0] != '0' else '1') + identity[1:]
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            header = out / 'wrong.h'
            header.write_text((self.sdk / 'include/tiku_portable_identity.h').read_text().replace(identity, other))
            source = out / 'main.c'
            source.write_text('int main(void) { return 0; }\n')
            for tuning in [[], ['-flto']]:
                result = subprocess.run(['cc', '-O2', *tuning, *data['consumer_flags'],
                    '-I' + str(self.sdk / 'include'), '-include', str(header), str(source),
                    '-Wl,--gc-sections', str(self.sdk / 'lib/libtiku_models.a'), '-o', str(out / 'bad')],
                    capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('tiku_portable_sdk_' + other, result.stderr)

    def test_integrity_and_missing_provider(self):
        """Reject changed archives and require the model's linked metrics provider."""
        with tempfile.TemporaryDirectory() as temp:
            copy = Path(temp) / 'sdk'
            shutil.copytree(self.sdk, copy)
            archive = copy / 'lib/libtiku_models.a'
            data = archive.read_bytes()
            archive.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
            with self.assertRaisesRegex(ValueError, 'checksum'):
                portable_sdk.validate(copy)
            archive.write_bytes(data)
            (copy / 'lib/libtiku_font_subset.a').unlink()
            with self.assertRaisesRegex(ValueError, 'inventory'):
                portable_sdk.validate(copy)
            source = Path(temp) / 'probe.c'
            source.write_text('int main(void) { return 0; }\n')
            result = subprocess.run(['cc', str(source), '-Wl,--whole-archive', str(archive),
                                     '-Wl,--no-whole-archive', '-o', str(Path(temp) / 'probe')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('tiku_measure_', result.stderr)

    def test_stale_member_and_reproducible_export(self):
        """Reject one stale member even when archive names and remaining members match."""
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            exported = out / 'export'
            export_portable_sdk.export(self.build, exported, 'ar', 'nm')
            self.assertEqual(json.loads((exported / 'sdk.json').read_text())['files_sha256'],
                             json.loads((self.sdk / 'sdk.json').read_text())['files_sha256'])
            archive = out / 'libtiku_models.a'
            shutil.copy2(self.build / 'lib/libtiku_models.a', archive)
            identity = json.loads((self.build / 'profile.json').read_text())['sdk_identity']
            names = portable_sdk.archive_identities(archive, 'ar', identity)
            original = subprocess.check_output(['ar', 'p', str(archive), names[0]])
            stale = ('0' if identity[0] != '0' else '1') + identity[1:]
            member = out / names[0]
            member.write_bytes(original.replace(identity.encode(), stale.encode()))
            subprocess.run(['ar', 'r', str(archive), str(member)], check=True)
            with self.assertRaisesRegex(ValueError, 'Stale archive member'):
                portable_sdk.archive_identities(archive, 'ar', identity, names)

    def test_lto_relocation(self):
        """Keep archive identity and complete public symbol coverage under LTO."""
        result = subprocess.run(['make', '-s', '-j4', 'FOUNDATION_PROFILE=portable-ui',
                                 'BUILD=' + str(self.out / 'lto'), 'CFLAGS=-O2 -g -flto', 'check-sdk'],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bounded_control_formatting(self):
        """Compare portable formatting to libc and reject corrupted boundary guards."""
        source_text = r'''
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "format.inl"
int main(void) {
    int values[] = {INT_MIN, INT_MAX, -1, 0, 1, -123456789, 123456789};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i) {
        for (size_t size = 0; size < 24; ++size) {
            char expected[32], actual[32];
            memset(expected, 0x55, sizeof expected); memset(actual, 0x55, sizeof actual);
            snprintf(expected, size, "%d", values[i]);
            tiku_control_decimal(actual, size, values[i]);
            if (memcmp(expected, actual, sizeof expected)) return 1;
        }
    }
    for (size_t size = 0; size < 24; ++size) {
        char expected[32], actual[32];
        memset(expected, 0x55, sizeof expected); memset(actual, 0x55, sizeof actual);
        snprintf(expected, size, "%s", "012345678901234567890123456789");
        tiku_control_label(actual, size, "012345678901234567890123456789");
        if (memcmp(expected, actual, sizeof expected)) return 2;
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            source = out / 'format.c'
            source.write_text(source_text)
            original = (ROOT / 'kits/interface/portable/tiku_control_text.inl').read_text()
            variants = [original, original.replace('0u - (unsigned)value', '(unsigned)value'),
                        original.replace('n + 1 < capacity', 'n < capacity')]
            for index, text in enumerate(variants):
                (out / 'format.inl').write_text(text)
                binary = out / ('format' + str(index))
                subprocess.run(['cc', '-O2', '-Wno-format-truncation', str(source), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)])
                self.assertEqual(result.returncode == 0, index == 0)


if __name__ == '__main__':
    unittest.main()
