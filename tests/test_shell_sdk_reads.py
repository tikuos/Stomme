#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check read-only stream retries without starting a native desktop."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
import ast
import os
import unittest

ROOT=Path(__file__).resolve().parents[1]


class ShellReadTests(unittest.TestCase):
    """Exercise the workflow's actual client closure with controlled transport results."""

    def client(self,operation,success=True):
        """Execute the client closure with one explicit stale refusal then success."""
        source=Path(os.environ.get('STOMME_SHELL_SDK_READ_FIXTURE',ROOT/'tests/integration/run_shell_resources_sdk.py'))
        tree=ast.parse(source.read_text())
        function=next(node for node in ast.walk(tree) if isinstance(node,ast.FunctionDef) and node.name=='client')
        transport=Mock()
        transport.run.side_effect=[SimpleNamespace(returncode=1,stdout='partial',stderr='stale_revision\n'),SimpleNamespace(returncode=0,stdout='complete\n',stderr='')]
        history=[]
        namespace=dict(temp=Path('/fixture'),control=Path('/control'),subprocess=transport,history=history,time=Mock())
        exec(compile(ast.Module(body=[function],type_ignores=[]),str(source),'exec'),namespace)
        result=namespace['client']('shell',operation,'/desktop/icon-layout',success=success)
        return result,transport,history

    def test_stream_restarts_collection(self):
        """A refused stream is retried and partial stdout is excluded from its result."""
        result,transport,history=self.client('stream')
        self.assertEqual(result,'complete')
        self.assertEqual(transport.run.call_count,2)
        self.assertEqual(history[0]['stdout'],'partial')
        self.assertEqual(len(history),2)

    def test_mutation_is_not_retried(self):
        """The read retry rule does not replay a failed action."""
        with self.assertRaises(AssertionError):self.client('action')

    def test_expected_failure_is_not_retried(self):
        """An intentionally refused operation returns its first recorded outcome."""
        result,transport,history=self.client('stream',success=False)
        self.assertEqual(result,'partial')
        self.assertEqual(transport.run.call_count,1)
        self.assertEqual(len(history),1)


if __name__=='__main__':unittest.main()
