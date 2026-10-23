#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Require shared shell ownership and product service configuration."""
import json
from test_product_bridge import ProductBridgeTests


class ShellPackageTests(ProductBridgeTests):
    """Compose shared runtime sources while retaining a product board adapter."""

    def setUp(self):
        """Create a package and a product with a local board adapter."""
        super().setUp()
        package = self.base / 'packages/shell'
        (package / 'system/desktop').mkdir(parents=True)
        (package / 'system/desktop/shared.c').write_text('shared shell')
        (package / 'tests/system').mkdir(parents=True)
        (package / 'tests/system/test_shared.c').write_text('shared regression')
        (package / 'package.json').write_text(json.dumps({
            'sources': ['system/desktop/shared.c'],
            'tests': ['tests/system/test_shared.c']}))
        (self.product / 'system/desktop').mkdir(parents=True)
        (self.product / 'system/desktop/board.c').write_text('product adapter')
        (self.product / 'system/product').mkdir()
        self.config = self.product / 'system/product/tiku_shell_product.h'
        self.config.write_text('#define TIKU_SHELL_BOARD_SERVICES 1\n'
                               '#define TIKU_SHELL_REMOTE_SERVICES 0\n'
                               '#define TIKU_SHELL_NOTE_TABS 0\n')
        (self.product / 'foundation.product.json').write_text(json.dumps({
            'document_provider': 'none', 'shell': 'desktop'}))
        from test_product_bridge import bridge
        self.lock['source_sha256'] = bridge.digest_files(bridge.source_files(self.base))
        self.save_lock()

    def test_reject_shell_copy(self):
        """Reject a migrated implementation restored inside the product."""
        (self.product / 'system/desktop/shared.c').write_text('inactive copy')
        with self.assertRaisesRegex(RuntimeError, 'Migrated product copy'):
            self.compose()

    def test_shared_shell_and_product_extension(self):
        """The selected shell and regression come from the foundation."""
        self.compose()
        self.assertEqual((self.output / 'system/desktop/shared.c').read_text(), 'shared shell')
        self.assertEqual((self.output / 'tests/system/test_shared.c').read_text(), 'shared regression')
        self.assertEqual((self.output / 'system/desktop/board.c').read_text(), 'product adapter')
        self.assertEqual((self.output / 'system/product/tiku_shell_product.h').read_bytes(), self.config.read_bytes())

    def test_missing_shell_configuration(self):
        """A selected shell requires a product configuration header."""
        self.config.unlink()
        with self.assertRaisesRegex(RuntimeError, 'shell configuration'):
            self.compose()

    def test_unknown_shell(self):
        """An unregistered shell package is rejected."""
        (self.product / 'foundation.product.json').write_text('{"document_provider":"none","shell":"missing"}')
        with self.assertRaisesRegex(RuntimeError, 'shell package'):
            self.compose()

    def test_invalid_service_selection(self):
        """Service flags must be explicit boolean constants."""
        self.config.write_text('#define TIKU_SHELL_BOARD_SERVICES 7\n')
        with self.assertRaisesRegex(RuntimeError, 'service selection'):
            self.compose()

    def test_unchanged_shell_composition(self):
        """Repeated composition preserves source link timestamps."""
        self.compose()
        path = self.output / 'tests/system/test_shared.c'
        stamp = path.lstat().st_mtime_ns
        self.compose()
        self.assertEqual(path.lstat().st_mtime_ns, stamp)

    def test_package_selection_invalidates_cached_sources(self):
        """Changing source ownership invalidates the product object configuration."""
        self.compose()
        stamp = (self.output / 'config.stamp').read_bytes()
        (self.product / 'foundation.product.json').write_text('{"document_provider":"none"}')
        self.compose()
        self.assertNotEqual((self.output / 'config.stamp').read_bytes(), stamp)
