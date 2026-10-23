#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check module SDK preflight before constructors execute."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = 'a' * 64


class SdkLoadingTests(unittest.TestCase):
    """Compile a loader and modules with observable constructor effects."""

    def setUp(self):
        """Compile the host loader without a desktop runtime."""
        temp = tempfile.TemporaryDirectory(prefix='sdk-preflight-')
        self.addCleanup(temp.cleanup)
        self.work = Path(temp.name)
        self.includes = ['-I' + str(ROOT / 'kits' / kit) for kit in ('application', 'interface', 'support')]
        (self.work / 'host.c').write_text('#include "tiku_app.h"\n#include <stdio.h>\n'
            'int main(int n, char **v) {char e[256]; (void)n; '
            'const tiku_app_descriptor_t *d=tiku_app_load(v[1], e, sizeof e); '
            'puts(e); return d ? 0 : 3;}\n')
        source = os.environ.get('FOUNDATION_LOADER_TEST_SOURCE', str(ROOT / 'kits/application/tiku_app_load.c'))
        subprocess.run(['cc', *self.includes, '-DTIKU_SDK_IDENTITY="' + IDENTITY + '"',
                        str(self.work / 'host.c'), source, '-ldl', '-o', str(self.work / 'host')], check=True)

    def module(self, identity, loader=False):
        """Compile a module whose constructor writes a marker file."""
        (self.work / 'module.c').write_text('#include "tiku_app.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
            '__attribute__((constructor)) static void started(void) {'
            'FILE *f=fopen(getenv("SDK_CTOR_MARKER"),"w"); if(f){fputs("started",f);fclose(f);}}\n'
            'static const tiku_app_descriptor_t app={.id="probe"};\n'
            'const tiku_app_export_t tiku_app_v1={TIKU_APP_ABI,sizeof(app),&app};\n')
        flags = [] if identity is None else ['-DTIKU_APP_SO', '-DTIKU_SDK_IDENTITY="' + identity + '"']
        sources = [str(self.work / 'module.c')]
        if loader:
            sources.append(str(ROOT / 'kits/application/tiku_app_load.c'))
        subprocess.run(['cc', '-fPIC', '-shared', *self.includes, *flags,
                        *sources, '-ldl', '-o', str(self.work / 'module.so')], check=True)

    def run_host(self):
        """Load the module in a process with its own constructor marker."""
        return subprocess.run([str(self.work / 'host'), str(self.work / 'module.so')],
                              env=dict(os.environ, SDK_CTOR_MARKER=str(self.work / 'started')),
                              capture_output=True, text=True)

    def test_matching_sdk(self):
        """Matching SDK bytes allow normal module startup."""
        self.module(IDENTITY)
        result = self.run_host()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.work / 'started').is_file())

    def test_module_with_loader(self):
        """A module can link the loader and retain its matching SDK record."""
        self.module(IDENTITY, loader=True)
        result = self.run_host()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_wrong_sdk_before_constructors(self):
        """A different SDK is refused before its constructor runs."""
        self.module('b' * 64)
        result = self.run_host()
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIn('incompatible application SDK', result.stdout)
        self.assertFalse((self.work / 'started').exists())

    def test_unmarked_sdk_before_constructors(self):
        """Unmarked modules require rebuilding against the selected SDK."""
        self.module(None)
        result = self.run_host()
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertFalse((self.work / 'started').exists())


if __name__ == '__main__':
    unittest.main()
