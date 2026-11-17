#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Exercise allocator, queue and time contracts against faulted implementations."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_dependencies
import check_portable

SOURCES = ['kits/support/portable/tiku_allocator.c',
           'kits/support/portable/tiku_clock.c',
           'kits/interface/portable/tiku_event_queue.c',
           'platform/host-test/tiku_test_pool.c', 'tests/portable/services.c']
INCLUDES = ['-Ikits/interface', '-Ikits/interface/portable',
            '-Ikits/support/portable', '-Iplatform/host-test']


class ServiceTests(unittest.TestCase):
    """Require positive workloads and independently injected failures."""

    def test_workload_and_mutations(self):
        """Reject lost ownership, stale events, early free and missing cancellation."""
        mutations = [
            (0, 'return 0;\n    }\n    if ((uintptr_t)data',
             'tiku_buffer_dispose(buffer); return 0;\n    }\n    if ((uintptr_t)data'),
            (0, 'memset((unsigned char *)data + copied, 0, bytes - copied);',
             'memset((unsigned char *)data + copied, 1, bytes - copied);'),
            (0, '(size && count > SIZE_MAX / size)', '0'),
            (2, 'ticket->generation == ticket->queue->generation', '1'),
            (2, '(*queue)->open || (*queue)->tickets', '(*queue)->open'),
            (2, 'queue->cancel = 1;', 'queue->cancel = 0;'),
            (1, 'now < clock->last', '0'),
        ]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            for index, mutation in enumerate([None] + mutations):
                sources = [str(ROOT / name) for name in SOURCES]
                if mutation:
                    which, before, after = mutation
                    text = Path(sources[which]).read_text()
                    self.assertIn(before, text)
                    changed = output / ('mutation' + str(index) + '.c')
                    changed.write_text(text.replace(before, after))
                    sources[which] = str(changed)
                binary = output / ('probe' + str(index))
                result = subprocess.run(['cc', '-O2', '-std=c11', '-Wall', '-Wextra',
                                         '-Werror', *INCLUDES, *sources, '-o', str(binary)],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True)
                if mutation:
                    self.assertNotEqual(result.returncode, 0, mutation)
                else:
                    self.assertEqual(result.returncode, 0)

    def test_narrow_support_exception(self):
        """Allow the allocator header but reject unrelated Support imports and calls."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            interface = root / 'kits/interface'
            support = root / 'kits/support/portable'
            interface.mkdir(parents=True)
            support.mkdir(parents=True)
            (support / 'tiku_allocator.h').write_text('')
            (support / 'tiku_clock.h').write_text('')
            source = interface / 'model.c'
            for brackets in [('"', '"'), ('<', '>')]:
                source.write_text('#include ' + brackets[0] + 'tiku_clock.h' + brackets[1] + '\n')
                self.assertTrue(check_dependencies.check(root))
                source.write_text('#include ' + brackets[0] + 'tiku_allocator.h' + brackets[1] + '\n')
                self.assertEqual(check_dependencies.check(root), [])
            (support / 'tiku_allocator.h').write_text('#include "tiku_clock.h"\n')
            self.assertTrue(check_dependencies.check(root))
            self.assertTrue(check_portable.includes(root, ['kits/interface/model.c']))
            (support / 'tiku_allocator.h').write_text('')
            source.write_text('extern int tiku_clock_now(void); int f(void){return tiku_clock_now();}')
            other = support / 'tiku_clock.c'
            other.write_text('int tiku_clock_now(void){return 0;}')
            objects = [source.with_suffix('.o'), other.with_suffix('.o')]
            for path, obj in zip([source, other], objects):
                subprocess.run(['cc', '-c', str(path), '-o', str(obj)], check=True)
            report = check_portable.check('nm', objects, dict(runtime=[], providers=[], target='host-test'))
            self.assertTrue(any('forbidden support symbol' in error for error in report['errors']))

    def test_heap_trap_negative(self):
        """An injected direct malloc call reaches the host trap."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'trap.c'
            source.write_text('#include <stdlib.h>\nextern unsigned tiku_service_heap_calls;\nint main(void){void *p=malloc(4); return p != 0 || tiku_service_heap_calls != 1;}')
            binary = root / 'trap'
            subprocess.run(['cc', '-fno-builtin', str(source), 'tests/portable/heap_trap.c',
                            '-Wl,--wrap=malloc', '-o', str(binary)], cwd=ROOT, check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
