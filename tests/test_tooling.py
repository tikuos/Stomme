#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise dependency rejection, SDK ownership and configuration invalidation."""
from pathlib import Path
from types import SimpleNamespace
import importlib.util
import json
import tempfile
import unittest
import sys
import subprocess
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

ROOT = Path(__file__).resolve().parents[1]


def tool(name):
    """Load one checkout-local build tool."""
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ToolTests(unittest.TestCase):
    """Check public tool contracts against temporary source and artifact trees."""

    def test_dependency_rejection(self):
        """Reject product paths, undeclared kit edges and missing includes."""
        checker = tool('check_dependencies')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for kit in checker.ALLOWS:
                (root / 'kits' / kit).mkdir(parents=True)
            source = root / 'kits/interface/control.c'
            source.write_text('#include "../../apps/widget.h"\n')
            self.assertTrue(any('product or external' in x for x in checker.check(root)))
            (root / 'kits/translation/codec.h').write_text('')
            source.write_text('#include "codec.h"\n')
            self.assertTrue(any('forbidden kit dependency' in x for x in checker.check(root)))
            source.write_text('#include "missing.h"\n')
            self.assertTrue(any('missing quoted include' in x for x in checker.check(root)))
            source.write_text('')
            self.assertEqual(checker.check(root), [])

    def test_angle_product_include_rejection(self):
        """Angle includes cannot reach a product through a relative path."""
        checker = tool('check_dependencies')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'kits/interface/control.c'
            source.parent.mkdir(parents=True)
            source.write_text('#include <../../apps/widget.h>\n')
            self.assertTrue(checker.check(root))

    def test_displaylist_generator_modes(self):
        """Desktop output matches its baked header and board generation is present."""
        script = ROOT / 'kits/interface/tools/gendl.py'
        table = ROOT / 'kits/interface/dl_ops.txt'
        desktop = subprocess.run([sys.executable, str(script), 'desktop', str(table)],
                                 capture_output=True, check=True)
        self.assertEqual(desktop.stdout, (ROOT / 'kits/interface/tiku_dl_ops.h').read_bytes())
        board = subprocess.run([sys.executable, str(script), 'board', str(table)],
                               capture_output=True, check=True)
        self.assertIn(b'tiku_draw_fill', board.stdout)
        self.assertIn(b'TIKU_DRAW_OPS_PROTOTYPES', board.stdout)

    def test_displayless_rejection(self):
        """Reject drawing headers in a displayless model."""
        checker = tool('check_dependencies')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / 'kits/interface'
            folder.mkdir(parents=True)
            (folder / 'tiku_gfx.h').write_text('')
            (folder / 'tiku_doc.c').write_text('#include "tiku_gfx.h"\n')
            self.assertTrue(any('displayless model' in x for x in checker.check(root)))

    def test_public_header_closure(self):
        """Exclude private dependencies and incomplete quoted include graphs."""
        exporter = tool('export_sdk')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / 'kits/interface'
            folder.mkdir(parents=True)
            (folder / 'hidden_private.h').write_text('')
            (folder / 'public.h').write_text('#include "hidden_private.h"\n')
            (folder / 'incomplete.h').write_text('#include "unknown.h"\n')
            (folder / 'complete.h').write_text('#include <stddef.h>\n')
            self.assertEqual(set(exporter.public_headers(root, ['interface'])), {'complete.h'})

    def test_displayless_fragment_rejection(self):
        """Apply model boundaries to implementation fragments included by models."""
        checker = tool('check_dependencies')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / 'kits/interface'
            folder.mkdir(parents=True)
            (folder / 'tiku_font.h').write_text('')
            (folder / 'tiku_doc_layout.inl').write_text('#include "tiku_font.h"\n')
            self.assertTrue(any('displayless model' in x for x in checker.check(root)))

    def test_sdk_output_ownership(self):
        """Refuse overwriting user directories and SDKs from another provider."""
        exporter = tool('export_sdk')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage, output = root / 'stage', root / 'sdk'
            stage.mkdir()
            output.mkdir()
            (output / 'keep').write_text('user data')
            with self.assertRaises(RuntimeError):
                exporter.publish(stage, output, 'desktop-foundation')
            (output / 'sdk.json').write_text(json.dumps({'foundation_id': 'another-provider'}))
            with self.assertRaises(RuntimeError):
                exporter.publish(stage, output, 'desktop-foundation')
            self.assertEqual((output / 'keep').read_text(), 'user data')

    def test_document_archive_rejected(self):
        """Refuse an undeclared document provider in the base SDK."""
        exporter = tool('export_sdk')
        with tempfile.TemporaryDirectory() as temp:
            args = SimpleNamespace(output=Path(temp) / 'sdk', copy=None,
                                   libraries=[Path(temp) / 'libtiku_document.a'])
            with self.assertRaises(RuntimeError):
                exporter.export(args)

    def test_build_configuration_cycle(self):
        """Invalidate A to B to A transitions and retain unchanged stamps."""
        build = tool('build_config')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'output'
            stamp = Path(build.configure(root, 'x11', '-O2'))
            a = stamp.read_text()
            first = stamp.stat().st_mtime_ns
            build.configure(root, 'x11', '-O2')
            self.assertEqual(stamp.stat().st_mtime_ns, first)
            build.configure(root, 'x11', '-O0')
            self.assertNotEqual(stamp.read_text(), a)
            build.configure(root, 'x11', '-O2')
            self.assertEqual(stamp.read_text(), a)
            self.assertGreater(stamp.stat().st_mtime_ns, first)

    def test_product_private_boundaries(self):
        """Reject private kit imports and document structs in shell sources."""
        checker = tool('check_dependencies')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'view/kits/document').mkdir(parents=True)
            (root / 'view/kits/document/model_private.h').write_text('')
            (root / 'apps').mkdir()
            (root / 'system').mkdir()
            app = root / 'apps/app.c'
            app.write_text('#include "model_private.h"\n')
            self.assertTrue(any('private kit header' in e for e in checker.check_product(root, root / 'view')))
            app.write_text('#include "../../system/private.h"\n')
            self.assertTrue(any('desktop-private' in e for e in checker.check_product(root, root / 'view')))
            app.write_text('')
            (root / 'system/shell.h').write_text('tiku_rich_char_t character;')
            self.assertTrue(any('model type' in e for e in checker.check_product(root, root / 'view')))
            (root / 'system/shell.h').write_text('void *document;')
            self.assertEqual(checker.check_product(root, root / 'view'), [])

    def test_identity_initializes_output(self):
        """A fresh identity output carries the build ownership marker."""
        identity = tool('standalone_identity')
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'build'
            identity.generate(ROOT, output / 'generated', 'none', 'x11', '-O2')
            self.assertTrue((output / '.foundation-build.json').is_file())

    def test_identity_rejects_unowned_output(self):
        """Identity generation preserves unowned directories before writing files."""
        identity = tool('standalone_identity')
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            (output / 'keep').write_text('user content')
            with self.assertRaisesRegex(RuntimeError, 'no foundation marker'):
                identity.generate(ROOT, output / 'generated', 'none', 'x11', '-O2')
            self.assertEqual(list(output.iterdir()), [output / 'keep'])

    def test_unowned_build_output(self):
        """Retain unmarked directories on both configure and clean."""
        build = tool('build_config')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'keep').write_text('user data')
            for clean in [False, True]:
                with self.assertRaises(RuntimeError):
                    build.configure(root, 'x11', '', clean)
            self.assertEqual((root / 'keep').read_text(), 'user data')


if __name__ == '__main__':
    unittest.main()
