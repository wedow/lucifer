"""Target-side observation only; keep the upstream test suite unchanged."""
import json
import os
import runpy
import sys
import time


def append(path, value):
    with open(path, 'a', encoding='utf-8') as stream:
        stream.write(json.dumps(value, ensure_ascii=True) + '\n')


def observe_cases():
    from test.libregrtest.testresult import RegressionTestResult

    starts = {}
    original_start = RegressionTestResult.startTest

    def start(self, test):
        starts[id(test)] = time.perf_counter()
        return original_start(self, test)

    RegressionTestResult.startTest = start

    def wrap(name, status):
        original = getattr(RegressionTestResult, name)

        def record(self, test, *args):
            result = original(self, test, *args)
            parent = getattr(test, 'test_case', test)
            reason = ''
            if args:
                reason = str(args[0]) if name == 'addSkip' else str(args[0][1])
            append(os.environ['LIBTEST_EVENTS'], {
                'id': parent.id(), 'status': status, 'reason': reason,
                'seconds': time.perf_counter() - starts.get(id(parent), time.perf_counter()),
            })
            return result

        setattr(RegressionTestResult, name, record)

    for name, status in [('addSuccess', 'PASS'), ('addFailure', 'FAIL'),
                         ('addError', 'ERROR'), ('addSkip', 'SKIP'),
                         ('addExpectedFailure', 'XFAIL'), ('addUnexpectedSuccess', 'XPASS')]:
        wrap(name, status)

    original_subtest = RegressionTestResult.addSubTest

    def subtest(self, test, subtest, err):
        result = original_subtest(self, test, subtest, err)
        if err is not None:
            append(os.environ['LIBTEST_EVENTS'], {
                'id': test.id(),
                'status': 'FAIL' if issubclass(err[0], test.failureException) else 'ERROR',
                'reason': str(err[1]),
                'seconds': time.perf_counter() - starts.get(id(test), time.perf_counter()),
            })
        return result

    RegressionTestResult.addSubTest = subtest


def main():
    if sys.argv[1:2] == ['--worker']:
        with open(os.environ['LIBTEST_PID'], 'w', encoding='ascii') as stream:
            stream.write(str(os.getpid()))
        observe_cases()
        # regrtest adds interpreter options before -m; those options already
        # apply to the parent. The actual test worker remains upstream's.
        index = sys.argv.index('-m')
        module = sys.argv[index + 1]
        sys.argv = [module, *sys.argv[index + 2:]]
        runpy.run_module(module, run_name='__main__')
    else:
        from test.libregrtest.results import TestResults
        from test.libregrtest.main import main as regrtest

        original = TestResults.accumulate_result

        def accumulate(self, result, runtests):
            append(os.environ['LIBTEST_STATE'], {
                'module': result.test_name, 'state': result.state,
                'seconds': result.duration, 'errors': result.errors,
                'failures': result.failures,
            })
            return original(self, result, runtests)

        TestResults.accumulate_result = accumulate
        regrtest(_add_python_opts=False)


if __name__ == '__main__':
    main()
