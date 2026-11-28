#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise presentation guards and renderer fixtures using isolated outputs."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_pixels


class RenderingTests(unittest.TestCase):
    """Compare real rendering and require faulted ownership paths to fail."""

    def test_native_rectangles(self):
        """Accepted and declined raster requests honor clip, stride and scale."""
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'rectangles'
            subprocess.run(['cc', '-std=c11', '-O2', '-ffunction-sections',
                            '-DTIKU_PORTABLE_UI=1', '-DTIKU_GFX_NATIVE_RASTER=1',
                            '-Ikits/interface', 'tests/portable/native_rectangles.c',
                            'kits/interface/tiku_gfx.c', '-Wl,--gc-sections', '-o', str(binary)],
                           cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)

    def test_rendering_and_mutations(self):
        """Reject early reuse, stale completion, lost cancellation and pixel corruption."""
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            build = out / 'build'
            result = subprocess.run(['make', '-s', '-j4', 'FOUNDATION_PROFILE=portable-ui',
                                     'BUILD=' + str(build), 'test', 'portable-panel'],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            pixels = subprocess.check_output([str(build / 'painting')])
            fixture = json.loads((ROOT / 'tests/portable/painting.json').read_text())
            self.assertTrue(check_pixels.check(pixels, fixture))
            self.assertFalse(check_pixels.check(bytes([pixels[0] ^ 1]) + pixels[1:], fixture))
            self.assertFalse(check_pixels.check(pixels[:-4], fixture))
            mutations = [
                ('kits/interface/portable/tiku_framebuffer.c',
                 '!frame || frame->state != FRAME_IDLE', '!frame'),
                ('kits/interface/portable/tiku_framebuffer.c',
                 'token != frame->view.token', '0'),
                ('examples/portable-panel/panel.c',
                 'tiku_slider_release(&panel->slider);', '(void)0;'),
            ]
            includes = ['-Ikits/interface', '-Ikits/interface/portable', '-Ikits/support/portable',
                        '-Iplatform/host-test', '-Iexamples/portable-panel']
            providers = ['examples/portable-panel/panel.c', 'platform/host-test/tiku_test_pool.c',
                         'platform/host-test/tiku_memory_presenter.c']
            for index, (source, before, after) in enumerate(mutations):
                text = (ROOT / source).read_text()
                self.assertIn(before, text)
                changed = out / ('mutation' + str(index) + '.c')
                changed.write_text(text.replace(before, after))
                objects = sorted((build / 'objects').rglob('*.o')) + sorted((build / 'fonts').glob('*.o'))
                objects += [p for p in (build / 'render').rglob('*.o') if p.stem != Path(source).stem]
                sources = [p for p in providers if p != source]
                binary = out / ('fault' + str(index))
                result = subprocess.run(['cc', '-O2', '-DTIKU_PORTABLE_UI=1', *includes,
                                         'tests/portable/rendering.c', *sources, str(changed),
                                         *map(str, objects), '-o', str(binary)],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, source)


if __name__ == '__main__':
    unittest.main()
