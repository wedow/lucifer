"""Target-side observation only; keep the upstream test suite unchanged."""
import json
import os
import runpy
import sys
import time


def append(path, value):
    with open(path, 'a', encoding='utf-8') as stream:
        stream.write(json.dumps(value, ensure_ascii=True) + '\n')


def described(exc_info):
    """'Type: message' for a failure, so raw events group by cause."""
    kind, value = exc_info[0], exc_info[1]
    return f'{getattr(kind, "__name__", kind)}: {value}'


def observe_cases():
    import unittest
    from test.libregrtest.testresult import RegressionTestResult

    starts = {}
    case_ids = {}
    original_start = RegressionTestResult.startTest

    def start(self, test):
        starts[id(test)] = time.perf_counter()
        case_ids[id(test)] = test.id()
        return original_start(self, test)

    RegressionTestResult.startTest = start
    original_stop = RegressionTestResult.stopTest

    def stop(self, test):
        try:
            return original_stop(self, test)
        finally:
            starts.pop(id(test), None)
            case_ids.pop(id(test), None)

    RegressionTestResult.stopTest = stop

    def case_id(test):
        if id(test) in case_ids:
            return case_ids[id(test)]
        value = test.id
        return value() if callable(value) else str(value)

    def wrap(name, status):
        original = getattr(RegressionTestResult, name)

        def record(self, test, *args):
            result = original(self, test, *args)
            parent = test.test_case if isinstance(test, unittest.case._SubTest) else test
            reason = ''
            if args:
                reason = str(args[0]) if name == 'addSkip' else described(args[0])
            append(os.environ['LIBTEST_EVENTS'], {
                'id': case_id(parent), 'status': status, 'reason': reason,
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
                'id': case_id(test),
                'status': 'FAIL' if issubclass(err[0], test.failureException) else 'ERROR',
                'reason': described(err),
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
        from test.libregrtest.runtests import RunTests
        from test.libregrtest.main import main as regrtest

        # Our command runs locally, so upstream's inherited descriptor works
        # with the wrapper too. stdout is unsafe: curses emits terminal bytes
        # during shutdown after the worker has serialized its result.
        if os.name == 'posix':
            RunTests.json_file_use_stdout = lambda self: False

        if '--list-cases' in sys.argv:
            # Upstream's list-cases branch skips setup_tests(), so its -u
            # flags otherwise have no effect on conditional case generation.
            from test import support
            from test.libregrtest.cmdline import _parse_args
            support.use_resources = dict(_parse_args(sys.argv[1:]).use_resources)

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
