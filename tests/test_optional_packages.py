#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Reject incompatible providers and general-codec dependencies on documents."""
import json
import unittest
from test_product_bridge import ProductBridgeTests


class OptionalPackageTests(ProductBridgeTests):
    """Exercise package selection through product source composition."""

    def test_conflicting_providers(self):
        """Refuse a profile selecting both document layouts."""
        (self.product / 'foundation.product.json').write_text(json.dumps({
            'document_provider': ['tiku-basic', 'kintsugi-advanced']}))
        with self.assertRaisesRegex(RuntimeError, 'document provider'):
            self.compose()

    def test_unknown_provider(self):
        """Refuse an unknown document provider before generating build inputs."""
        (self.product / 'foundation.product.json').write_text('{"document_provider":"other"}')
        with self.assertRaisesRegex(RuntimeError, 'document provider'):
            self.compose()

    def test_unselected_provider_source(self):
        """A base product cannot carry an undeclared document provider."""
        path = self.product / 'kits/document/extra.c'
        path.parent.mkdir()
        path.write_text('provider source')
        with self.assertRaisesRegex(RuntimeError, 'Unselected product provider'):
            self.compose()

    def test_formats_are_optional(self):
        """Document adapters have a separate archive source group."""
        (self.product / 'foundation.product.json').write_text('{"document_provider":"kintsugi-advanced"}')
        folder = self.product / 'kits/document'
        folder.mkdir()
        (folder / 'tiku_richdoc.h').write_text('')
        (folder / 'tiku_richdoc.c').write_text('')
        (self.product / 'kits/translation/tiku_richio.c').write_text('')
        self.compose()
        lines = (self.output / 'sources.mk').read_text().splitlines()
        codecs = next(line for line in lines if line.startswith('TRANSLATION :='))
        self.assertNotIn('tiku_richio.c', codecs)
        self.assertTrue(any(line.startswith('DOCUMENT_FORMATS :=') and 'tiku_richio.c' in line for line in lines))


if __name__ == '__main__':
    unittest.main()
