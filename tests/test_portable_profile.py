#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise profile isolation, frozen font fidelity and dependency rejection."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_portable
import portable_profile
import subset_fonts


class PortableTests(unittest.TestCase):
    """Compile independent failing fixtures before accepting the profile guards."""

    def setUp(self):
        """Keep generated artifacts outside shared build outputs."""
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        self.profile = json.loads((ROOT / 'profiles/portable-ui.json').read_text())
        self.profile['target'] = 'host-test'

    def run_command(self, command, **kwargs):
        """Collect diagnostics for commands expected to succeed or fail."""
        return subprocess.run(command, cwd=ROOT, capture_output=True, text=True, **kwargs)

    def compile(self, name, source):
        """Compile a dependency fixture with calls retained."""
        path = self.out / (name + '.c')
        path.write_text(source)
        obj = path.with_suffix('.o')
        result = self.run_command(['cc', '-O0', '-fno-builtin', '-c', str(path), '-o', str(obj)])
        self.assertEqual(result.returncode, 0, result.stderr)
        return obj

    def test_reject_direct_heap_native_and_unselected_font(self):
        """Reject direct allocations, native services and omitted font tables."""
        for name, source, expected in [
            ('heap', '#include <stdlib.h>\nvoid *f(void){return malloc(8);}', 'malloc'),
            ('native', 'extern int pthread_join(int,void*); int f(void){return pthread_join(0,0);}', 'pthread_join'),
            ('font', 'int plain2x_font = 1;', 'unselected font')]:
            obj = self.compile(name, source)
            report = check_portable.check('nm', [obj], self.profile)
            self.assertTrue(any(expected in error for error in report['errors']), report)
        good = self.compile('good', 'int f(void){return 7;}')
        self.assertEqual(check_portable.check('nm', [good], self.profile)['errors'], [])

    def test_reject_resolved_indirect_heap(self):
        """A helper archive cannot hide a heap call by defining malloc itself."""
        helper = self.compile('helper', '#include <stddef.h>\nstatic char memory[16];\nvoid *malloc(size_t n){return n < 16 ? memory : 0;}\nvoid *helper(void){return malloc(8);}')
        lib = self.out / 'helper.a'
        subprocess.run(['ar', 'rcs', str(lib), str(helper)], check=True)
        main = self.compile('main', 'extern void *helper(void); int main(void){return helper() == 0;}')
        binary = self.out / 'probe'
        result = self.run_command(['cc', str(main), str(lib), '-o', str(binary)])
        self.assertEqual(result.returncode, 0, result.stderr)
        defined, undefined = check_portable.symbols('nm', binary)
        self.assertIn('malloc', defined)
        self.assertNotIn('malloc', undefined)
        self.assertTrue(check_portable.check('nm', [binary], self.profile, linked=True)['errors'])

    def test_include_rejection(self):
        """Reject an indirect pthread include through an otherwise local header."""
        path = self.out / 'kits/interface'
        path.mkdir(parents=True)
        (path / 'model.c').write_text('#include "helper.h"\n')
        (path / 'helper.h').write_text('#include <pthread.h>\n')
        self.assertTrue(check_portable.includes(self.out, ['kits/interface/model.c']))
        (path / 'helper.h').write_text('#include <stdint.h>\n')
        self.assertEqual(check_portable.includes(self.out, ['kits/interface/model.c']), [])

    def test_invalid_target_and_provider(self):
        """Reject unsupported target, compiler, provider and desktop combinations."""
        for target in ['unknown', 's31']:
            with self.assertRaises(ValueError):
                portable_profile.select(ROOT, target, 'cc')
        for extra in [['DOCUMENT_PROVIDER=tiku-basic'], ['TIKU_HOST=x11'], ['FOUNDATION_TARGET=s31'], ['FOUNDATION_TARGET=unknown']]:
            result = self.run_command(['make', '-n', 'FOUNDATION_PROFILE=portable-ui', 'BUILD=' + str(self.out / 'build'), *extra])
            self.assertNotEqual(result.returncode, 0)
        result = self.run_command(['make', '-n', 'FOUNDATION_TARGET=s31'])
        self.assertNotEqual(result.returncode, 0)

    def test_no_native_discovery(self):
        """Portable configuration does not invoke uname or pkg-config."""
        trap = self.out / 'native-discovery'
        for name in ['uname', 'pkg-config']:
            script = self.out / name
            script.write_text('#!/bin/sh\ntouch "' + str(trap) + '"\nexit 91\n')
            script.chmod(0o755)
        result = self.run_command(['make', '-n', 'FOUNDATION_PROFILE=portable-ui', 'BUILD=' + str(self.out / 'build')], env=dict(os.environ, PATH=str(self.out) + ':' + os.environ['PATH']))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(trap.exists())
        for dependency in ['-lX11', '-pthread', 'gio-2.0', '-framework']:
            self.assertNotIn(dependency, result.stdout)

    def test_frozen_fonts_and_corruption(self):
        """Compare compiled glyphs with desktop data and reject one changed byte."""
        manifest = ROOT / 'profiles/portable-fonts.json'
        outputs = []
        for name in ['one', 'two']:
            out = self.out / name
            result = self.run_command([sys.executable, 'tools/subset_fonts.py', '--manifest', str(manifest), '--output', str(out)])
            self.assertEqual(result.returncode, 0, result.stderr)
            outputs.append((out / 'tiku_font_subset_data.c').read_bytes())
        self.assertEqual(*outputs)
        report = json.loads((self.out / 'one/fonts.json').read_text())
        self.assertEqual(sum(f['coverage_bytes'] + f['record_bytes'] for f in report['faces']), 47648)
        data = self.out / 'one/tiku_font_subset_data.c'
        binary = self.out / 'font-reference'
        command = ['cc', '-O2', '-Ikits/interface', '-Ikits/interface/portable',
                   'tests/portable/font_reference.c', 'kits/interface/portable/tiku_font_subset.c',
                   str(data), '-o', str(binary)]
        result = self.run_command(command)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.run_command([str(binary)]).returncode, 0)
        text = data.read_text()
        text = re.sub(r'(plain_bits\[\] = \{\s*)(\d+)', lambda m: m[1] + str(int(m[2]) ^ 1), text, count=1)
        data.write_text(text)
        result = self.run_command(command)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.run_command([str(binary)]).returncode, 0)
        spec = json.loads(manifest.read_text())
        spec['source_sha256'] = '0' * 64
        bad = self.out / 'bad-fonts.json'
        bad.write_text(json.dumps(spec))
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            subset_fonts.generate(ROOT, bad)

    def test_missing_metrics_provider(self):
        """Linking a control without its required metrics provider fails."""
        obj = self.out / 'slider.o'
        result = self.run_command(['cc', '-DTIKU_DISPLAYLESS=1', '-Ikits/interface', '-c', 'kits/interface/tiku_slider.c', '-o', str(obj)])
        self.assertEqual(result.returncode, 0, result.stderr)
        main = self.compile('entry', 'int main(void){return 0;}')
        result = self.run_command(['cc', str(main), str(obj), '-o', str(self.out / 'missing')])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('tiku_measure_step_w', result.stderr)


if __name__ == '__main__':
    unittest.main()
