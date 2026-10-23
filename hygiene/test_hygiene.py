#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check commit message rules and installed hooks in disposable repositories."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('commit_rules', ROOT / 'hygiene/check_commit_msg.py')
rules = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rules)


class MessageTests(unittest.TestCase):
    """Exercise the accepted message format and rejected metadata."""

    def test_valid_message(self):
        """A named capability accepts up to three plain bullet points."""
        self.assertEqual(rules.check('Stomme: Shared desktop framework\n\n- Native kits and SDK\n- Common desktop components\n- Checked application boundaries\n'), [])

    def test_rejected_messages(self):
        """Reject malformed subjects, prose bodies, excess bullets and trailers."""
        for message in ['', 'subject', 'SDK: ' + 'x' * 72,
                        'SDK: Public headers.', 'SDK: Fix exports',
                        'SDK: F7 complete', 'SDK: Public headers\nNo separator',
                        'SDK: Public headers\n\nProse paragraph',
                        'SDK: Public headers\n\n- One\n- Two\n- Three\n- Four',
                        'SDK: Public headers\n\nCo-Authored-By: Agent <agent@example.test>',
                        'SDK: Public headers -- more headers']:
            with self.subTest(message=message):
                self.assertTrue(rules.check(message))


class HookTests(unittest.TestCase):
    """Install a repository-local hook and exercise its executable contract."""

    def setUp(self):
        """Create a checkout with a fixed configured author and no commits."""
        temporary = tempfile.TemporaryDirectory(prefix='stomme hygiene ')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith('GIT_') and k not in {'EMAIL'}}
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True, env=self.env)
        for key, value in [('user.name', 'Fixture Author'), ('user.email', 'fixture@example.test')]:
            subprocess.run(['git', '-C', str(self.root), 'config', key, value], check=True, env=self.env)
        shutil.copytree(ROOT / 'hygiene', self.root / 'hygiene',
                        ignore=shutil.ignore_patterns('__pycache__'))
        self.message = self.root / 'message'
        self.message.write_text('Stomme: Shared desktop framework\n\n- Native kits and SDK\n')
        self.run_command(['sh', 'hygiene/install_hooks.sh'])

    def run_command(self, command, expected=0):
        """Run one bounded fixture command and verify its exit status."""
        result = subprocess.run(command, cwd=self.root, env=self.env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_installed_hook(self):
        """The configured hook path resolves inside the cloned repository."""
        path = self.run_command(['git', 'config', '--local', 'core.hooksPath']).stdout.strip()
        hook = Path(path) if Path(path).is_absolute() else self.root / path
        self.assertEqual(hook.resolve(), self.root / 'hygiene/githooks')
        self.run_command(['git', 'hook', 'run', 'commit-msg', '--', str(self.message)])
        self.message.write_text('bad subject\n')
        self.run_command(['git', 'hook', 'run', 'commit-msg', '--', str(self.message)], 1)

    def test_missing_checker_rejected(self):
        """A missing checker cannot permit an unchecked commit."""
        (self.root / 'hygiene/check_commit_msg.py').unlink()
        self.run_command(['git', 'hook', 'run', 'commit-msg', '--', str(self.message)], 1)

    def test_author_override_rejected(self):
        """An email override differs from the repository's configured identity."""
        self.run_command(['git', '-c', 'user.email=other@example.test', 'hook',
                          'run', 'commit-msg', '--', str(self.message)], 1)

    def test_author_name_override_rejected(self):
        """A name override differs from the configured author identity."""
        self.run_command(['git', '-c', 'user.name=Other Author', 'hook',
                          'run', 'commit-msg', '--', str(self.message)], 1)
