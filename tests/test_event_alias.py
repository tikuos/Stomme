#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Compile shared input contracts beside a kernel-owned event typedef."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EventAliasTests(unittest.TestCase):
    """Keep desktop aliases while allowing canonical input types beside kernel events."""

    def compile(self, text, flags):
        """Compile a complete translation unit without native or firmware providers."""
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'events.c'
            source.write_text(text)
            return subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                   '-Ikits/interface', *flags, '-fsyntax-only', str(source)],
                                  cwd=ROOT, capture_output=True, text=True)

    def test_default_desktop_alias(self):
        """Both default and explicit zero retain the application event record."""
        source = '#include "tiku_event.h"\n_Static_assert(sizeof(tiku_event_t) == sizeof(tiku_input_event_t), "alias");\n'
        for flags in [[], ['-DTIKU_INPUT_NO_EVENT_ALIAS=0']]:
            result = self.compile(source, flags)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_kernel_event_in_both_include_orders(self):
        """Disabling the alias preserves kernel bytes and normalized input records."""
        kernel = '#include <stdint.h>\ntypedef uint8_t tiku_event_t;\n'
        controls = ''.join('#include "' + name + '"\n' for name in
                           ['tiku_list.h', 'tiku_choice.h', 'tiku_dialog.h', 'tiku_field_event.h'])
        checks = ('_Static_assert(sizeof(tiku_event_t) == 1, "kernel event");\n'
                  '_Static_assert(sizeof(tiku_input_event_t) > 1, "input record");\n'
                  '_Static_assert(TIKU_KEY_RETURN == 12 && TIKU_MOD_SHIFT == 1, "logical keys");\n')
        for source in [kernel + controls + checks, controls + kernel + checks]:
            for displayless in [[], ['-DTIKU_DISPLAYLESS=1']]:
                result = self.compile(source, ['-DTIKU_INPUT_NO_EVENT_ALIAS=1', *displayless])
                self.assertEqual(result.returncode, 0, result.stderr)
            result = self.compile(source, [])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('conflicting types', result.stderr)


if __name__ == '__main__':
    unittest.main()
