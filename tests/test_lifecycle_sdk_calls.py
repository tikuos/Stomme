#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check record retries against explicit native mutation effect reports."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
import ast
import os
import unittest

ROOT=Path(__file__).resolve().parents[1]


class LifecycleCallTests(unittest.TestCase):
    """Execute the workflow closure with controlled protocol acknowledgments."""

    def call(self,operation='record',error='settings job: failed busy none',success=True):
        """Offer one refusal and one successful result to the actual client helper."""
        source=Path(os.environ.get('STOMME_LIFECYCLE_SDK_FIXTURE',ROOT/'tests/integration/run_lifecycle_sdk.py'))
        function=next(node for node in ast.walk(ast.parse(source.read_text())) if isinstance(node,ast.FunctionDef) and node.name=='call')
        transport=Mock()
        transport.run.side_effect=[SimpleNamespace(returncode=1,stdout='',stderr=error+'\n'),SimpleNamespace(returncode=0,stdout='accepted\n',stderr='')]
        history=[]
        namespace=dict(work=Path('/fixture'),control=Path('/control'),subprocess=transport,history=history,time=Mock())
        exec(compile(ast.Module(body=[function],type_ignores=[]),str(source),'exec'),namespace)
        result=namespace['call'](operation,'/session/records/session/fixture/value','session','/fixture',success=success)
        return result,transport,history

    def test_unapplied_record_retry(self):
        """A busy record with no effect is retried with both attempts retained."""
        result,transport,history=self.call()
        self.assertEqual(result,'accepted')
        self.assertEqual(transport.run.call_count,2)
        self.assertEqual(len(history),2)

    def test_uncertain_effect_is_not_replayed(self):
        """Unknown and partial effects stop the workflow."""
        for effect in ['unknown','partial']:
            with self.subTest(effect=effect),self.assertRaises(AssertionError):
                self.call(error='settings job: failed busy '+effect)

    def test_action_is_not_replayed(self):
        """This record retry rule does not replay actions."""
        with self.assertRaises(AssertionError):self.call(operation='action')

    def test_expected_refusal_is_not_replayed(self):
        """An expected negative result retains its first attempt."""
        result,transport,history=self.call(success=False)
        self.assertEqual(result,'')
        self.assertEqual(transport.run.call_count,1)
        self.assertEqual(len(history),1)


if __name__=='__main__':unittest.main()
