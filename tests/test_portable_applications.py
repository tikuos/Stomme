#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Require lifecycle and paged-storage guards to reject corrupted implementations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ApplicationTests(unittest.TestCase):
    """Compile mutations separately and run the same ownership workload."""

    def test_applications_and_mutations(self):
        """Detect early teardown, cancelled delivery, count overflow and stale pages."""
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            build = out / 'build'
            result = subprocess.run(['make', '-s', '-j4', 'FOUNDATION_PROFILE=portable-ui',
                                     'BUILD=' + str(build), 'check-applications'],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            mutations = [
                ('kits/application/portable/tiku_static_app.c',
                 'app->phase != APP_STOPPED || app->leases', 'app->phase != APP_STOPPED'),
                ('kits/application/portable/tiku_static_app.c',
                 'app->phase == APP_RUNNING || app->phase == APP_STARTING', '1'),
                ('kits/application/portable/tiku_static_app.c',
                 'count < budget &&', '1 &&'),
                ('kits/storage/portable/tiku_paged_storage.c',
                 'if (store->cancelled) return 1;', 'if (0) return 1;'),
                ('kits/storage/portable/tiku_paged_storage.c',
                 'count > store->request.capacity', '0'),
                ('kits/storage/portable/tiku_paged_storage.c',
                 'entry->node_id <= previous', 'entry->node_id < previous'),
                ('kits/storage/portable/tiku_paged_storage.c',
                 'token != store->request.token', '0'),
            ]
            includes = ['kits/interface', 'kits/interface/portable', 'kits/support',
                        'kits/support/portable', 'kits/storage', 'kits/storage/portable',
                        'kits/application/portable', 'packages/apps/models', 'platform/host-test',
                        'examples/portable-browser', 'examples/portable-calculator']
            providers = ['tests/portable/metrics.c', 'platform/host-test/tiku_test_pool.c',
                         'examples/portable-browser/browser.c', 'examples/portable-calculator/calculator.c']
            for index, (source, before, after) in enumerate(mutations):
                text = (ROOT / source).read_text()
                self.assertIn(before, text)
                changed = out / ('mutation' + str(index) + '.c')
                changed.write_text(text.replace(before, after))
                objects = [p for p in (build / 'objects').rglob('*.o') if p.stem != Path(source).stem]
                binary = out / ('fault' + str(index))
                result = subprocess.run(['cc', '-O2', *['-I' + p for p in includes],
                                         'tests/portable/applications.c', *providers, str(changed),
                                         *map(str, objects), '-o', str(binary)],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, source + ': ' + before)


if __name__ == '__main__':
    unittest.main()
