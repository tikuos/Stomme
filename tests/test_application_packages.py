#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Require shared application ownership and product configuration in source views."""
import json
from test_product_bridge import ProductBridgeTests


class ApplicationPackageTests(ProductBridgeTests):
    """Compose distinct implementations and reject missing package selections."""

    def setUp(self):
        """Create a shared Calculator and its product descriptor and assets hook."""
        super().setUp()
        package = self.base / 'packages/apps'
        (package / 'apps/calc').mkdir(parents=True)
        (package / 'apps/calc/shared.c').write_text('shared application')
        (package / 'package.json').write_text(json.dumps({'applications':['calc'], 'editor_common':[], 'tests':[]}))
        (self.product / 'apps/calc').mkdir(parents=True)
        (self.product / 'apps/calc/calc.app').write_text('product manifest')
        (self.product / 'apps/shared').mkdir()
        (self.product / 'apps/shared/tiku_app_product.h').write_text('product assets')
        (self.product / 'foundation.product.json').write_text(json.dumps({'document_provider':'none','applications':['calc']}))
        self.lock['source_sha256'] = self.hash_sources()
        self.save_lock()

    def hash_sources(self):
        """Return the fixture's selected source revision."""
        from test_product_bridge import bridge
        return bridge.digest_files(bridge.source_files(self.base))

    def test_reject_application_copy(self):
        """Reject a migrated implementation restored inside the product."""
        (self.product / 'apps/calc/shared.c').write_text('inactive copy')
        with self.assertRaisesRegex(RuntimeError, 'Migrated product copy'):
            self.compose()

    def test_shared_owner_and_product_data(self):
        """The implementation comes from the package and metadata from the product."""
        self.compose()
        self.assertEqual((self.output / 'apps/calc/shared.c').read_text(),'shared application')
        self.assertEqual((self.output / 'apps/calc/calc.app').read_text(),'product manifest')
        self.assertEqual((self.output / 'apps/shared/tiku_app_product.h').read_text(),'product assets')

    def test_required_application_package(self):
        """Selecting an app requires its declared shared app dependencies."""
        path = self.base / 'packages/apps/package.json'
        metadata = json.loads(path.read_text())
        metadata['requires'] = {'calc': ['reader']}
        path.write_text(json.dumps(metadata))
        self.lock['source_sha256'] = self.hash_sources()
        self.save_lock()
        with self.assertRaisesRegex(RuntimeError, 'Missing application dependency'):
            self.compose()

    def test_unknown_application_package(self):
        """An undeclared source package fails composition before compilation."""
        (self.product / 'foundation.product.json').write_text('{"document_provider":"none","applications":["missing"]}')
        with self.assertRaisesRegex(RuntimeError,'application package'):
            self.compose()

    def test_missing_product_configuration(self):
        """A shared product app requires its own assets and defaults header."""
        (self.product / 'apps/shared/tiku_app_product.h').unlink()
        with self.assertRaisesRegex(RuntimeError,'configuration'):
            self.compose()
