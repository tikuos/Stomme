#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check source pins, product providers and configuration invalidation."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest
import sys
from unittest.mock import patch
from subprocess import CompletedProcess
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('bridge', ROOT / 'tools/product_bridge.py')
bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bridge)


class ProductBridgeTests(unittest.TestCase):
    """Build source views in disposable trees with conflicting kit files."""

    def setUp(self):
        """Create foundation and product sources with distinct contents."""
        temp = tempfile.TemporaryDirectory(prefix='foundation-bridge-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.base = self.root / 'base'
        self.product = self.root / 'product'
        for root in (self.base, self.product):
            (root / 'kits/interface').mkdir(parents=True)
            (root / 'kits/translation').mkdir()
        (self.base / 'kits/interface/shared.c').write_text('shared')
        for name in ('tiku_vector_raster.c', 'tiku_vector_raster.h'):
            (self.base / 'kits/interface' / name).write_text('')
        (self.base / 'foundation.json').write_text('{"id":"desktop-foundation"}')
        (self.product / 'kits/translation/provider.c').write_text('provider')
        self.lock = dict(foundation_id='desktop-foundation', source_sha256=bridge.digest_files(bridge.source_files(self.base)))
        self.save_lock()
        (self.product / 'foundation.product.json').write_text('{"document_provider":"none"}')
        self.output = self.root / 'view'

    def save_lock(self):
        """Write the selected source pin."""
        (self.product / 'foundation.lock.json').write_text(json.dumps(self.lock))

    def compose(self, flags='cc -O2', allow=False):
        """Compose the fixture with one host configuration."""
        return bridge.compose(self.base, self.product, self.output, 'x11', flags, allow)

    def test_reject_changed_source(self):
        """Changed foundation bytes require an updated pin or explicit override."""
        (self.base / 'kits/interface/shared.c').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'source differs'):
            self.compose()
        self.compose(allow=True)
        self.assertTrue(json.loads((self.output / 'identity.json').read_text())['development'])

    def test_source_ownership(self):
        """Shared paths use the foundation and provider paths use the product."""
        self.compose()
        self.assertEqual((self.output / 'kits/interface/shared.c').resolve(), self.base / 'kits/interface/shared.c')
        self.assertEqual((self.output / 'kits/translation/provider.c').resolve(), self.product / 'kits/translation/provider.c')
        self.assertIn('kits/interface/shared.c', (self.output / 'sources.mk').read_text())

    def test_reject_migrated_product_copy(self):
        """A product copy cannot shadow the selected foundation implementation."""
        (self.product / 'kits/interface/shared.c').write_text('inactive')
        with self.assertRaisesRegex(RuntimeError, 'Migrated product copy'):
            self.compose()

    def test_configuration_cycle(self):
        """Returning to prior flags invalidates the intervening configuration."""
        self.compose()
        a = (self.output / 'config.stamp').read_bytes()
        self.compose('-O0')
        b = (self.output / 'config.stamp').read_bytes()
        self.assertNotEqual(a, b)
        self.compose()
        self.assertEqual(a, (self.output / 'config.stamp').read_bytes())

    def test_reject_unowned_output(self):
        """Source preparation preserves unrelated output contents."""
        self.output.mkdir()
        (self.output / 'keep').write_text('keep')
        with self.assertRaisesRegex(RuntimeError, 'unrecognized output'):
            self.compose()
        self.assertEqual((self.output / 'keep').read_text(), 'keep')

    def test_export_does_not_inherit_parent_revision(self):
        """An exported source directory cannot claim its enclosing Git revision."""
        def git_result(command, **kwargs):
            """Return an enclosing checkout and its unrelated commit."""
            value = str(self.root) if '--show-toplevel' in command else 'a' * 40
            return CompletedProcess(command, 0, value + '\n', '')

        with patch.object(bridge.subprocess, 'run', side_effect=git_result):
            self.compose()
        self.assertIsNone(json.loads((self.output / 'identity.json').read_text())['source_revision'])

    def test_checkout_records_its_own_revision(self):
        """A foundation checkout records its own resolved Git revision."""
        def git_result(command, **kwargs):
            """Return the selected checkout and its commit."""
            value = str(self.base) if '--show-toplevel' in command else 'b' * 40
            return CompletedProcess(command, 0, value + '\n', '')

        with patch.object(bridge.subprocess, 'run', side_effect=git_result):
            self.compose()
        self.assertEqual(json.loads((self.output / 'identity.json').read_text())['source_revision'], 'b' * 40)

    def test_export_builds_without_git(self):
        """Content-pinned exports compose when Git is unavailable."""
        with patch.object(bridge.subprocess, 'run', side_effect=FileNotFoundError('git')):
            self.compose()
        self.assertIsNone(json.loads((self.output / 'identity.json').read_text())['source_revision'])


if __name__ == '__main__':
    unittest.main()
