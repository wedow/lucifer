"""Observe the upstream regrtest runner, including its parallel wrapper."""
import atexit
import json
import os
from pathlib import Path
import runpy
import sys
import sysconfig


def emit(value):
    with open(os.environ['FT_OBSERVATIONS'], 'a', encoding='utf-8') as stream:
        stream.write(json.dumps(value) + '\n')


def gil(stage):
    emit({'stage': stage, 'free_threaded': sysconfig.get_config_var('Py_GIL_DISABLED') == 1,
          'gil_enabled': getattr(sys, '_is_gil_enabled', lambda: True)()})


if __name__ == '__main__':
    if sys.argv[1:2] == ['--worker']:
        from test.libregrtest.parallel_case import ParallelTestCase
        from test.libregrtest.testresult import RegressionTestResult
        # Preserve the real case ID: unittest's inherited id() names the wrapper.
        ParallelTestCase.id = lambda self: self.test_case.id()
        original = ParallelTestCase.run

        def run(self, result=None):
            before = {k: len(getattr(result, k)) for k in
                      ('errors', 'failures', 'skipped', 'expectedFailures', 'unexpectedSuccesses')}
            value = original(self, result)
            # Upstream aggregates these lists without addSkip/addFailure calls.
            # Observe them so skipped/xfail copies cannot become PASS rows.
            for name, status in [('errors', 'ERROR'), ('failures', 'FAIL'),
                                 ('skipped', 'SKIP'), ('expectedFailures', 'XFAIL'),
                                 ('unexpectedSuccesses', 'XPASS')]:
                for entry in getattr(result, name)[before[name]:]:
                    case = entry if name == 'unexpectedSuccesses' else entry[0]
                    with open(os.environ['LIBTEST_EVENTS'], 'a', encoding='utf-8') as stream:
                        stream.write(json.dumps({'id': case.id(), 'status': status, 'seconds': 0}) + '\n')
            emit({'id': self.id(), 'threads': self.num_threads})
            return value

        ParallelTestCase.run = run
        old_start = RegressionTestResult.startTest

        def start(self, test):
            gil('case')
            if not isinstance(test, ParallelTestCase):
                emit({'id': test.id(), 'serial': True})
            return old_start(self, test)

        RegressionTestResult.startTest = start
        atexit.register(gil, 'completion')
    else:
        threads = int(os.environ.get('FT_THREADS', '1'))
        if threads > 1:
            sys.argv[1:1] = ['--parallel-threads', str(threads)]
        if '--randseed' in sys.argv:
            sys.argv[sys.argv.index('--randseed') + 1] = os.environ.get('FT_SEED', '0')
    runpy.run_path(str(Path(__file__).resolve().parents[1] / 'libtest' / 'bridge.py'), run_name='__main__')
