"""Run upstream unittest loaders, preserving doctests and suite wrappers."""
from collections import Counter
import importlib
import importlib.util
import os
from pathlib import Path
import signal
import sys
import time
import unittest

from recorder import canonical, emit, pytest_sessionfinish

SOURCE = Path(os.environ['CORPUS_SOURCE'])
PACKAGE = os.environ['CORPUS_PACKAGE']


def load():
    if PACKAGE == 'more-itertools':
        return unittest.defaultTestLoader.discover(str(SOURCE / 'tests'), top_level_dir=str(SOURCE))
    if PACKAGE == 'simplejson':
        import simplejson
        simplejson.__path__.insert(0, str(SOURCE / 'simplejson'))
        return importlib.import_module('simplejson.tests').all_tests_suite(project_dir=str(SOURCE))
    if PACKAGE == 'regex':
        import regex
        regex.__path__.insert(0, str(SOURCE / 'regex'))
        return unittest.defaultTestLoader.loadTestsFromName('regex.tests.test_regex')
    if PACKAGE == 'pytz':
        sys.path.insert(0, str(SOURCE / 'pytz/tests'))
        return importlib.import_module('test_tzinfo').test_suite()
    if PACKAGE == 'lxml':
        import lxml
        import lxml.html
        lxml.__path__.append(str(SOURCE / 'src/lxml'))
        lxml.html.__path__.append(str(SOURCE / 'src/lxml/html'))
        spec = importlib.util.spec_from_file_location('upstream_lxml_runner', SOURCE / 'test.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        cfg = runner.Options()
        cfg.basedir = str(SOURCE / 'src')
        cfg.src_in_path = False
        cfg.unit_tests = True
        files = runner.get_test_files(cfg)
        hooks = runner.get_test_hooks(files, cfg)
        if hooks:
            raise ValueError('new upstream lxml hooks require explicit support')
        return unittest.TestSuite(runner.get_test_cases(files, cfg))
    raise ValueError('unsupported unittest suite')


def flatten(suite, prefix=''):
    if type(suite).__name__ == 'NoExtensionTestSuite':
        prefix = 'python/'
    elif PACKAGE == 'simplejson' and not prefix:
        prefix = 'native/'
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from flatten(item, prefix)
        else:
            if type(item).__name__ == '_FailedTest':
                raise ValueError('upstream unittest loader failed: ' + item.id())
            yield item, prefix + canonical(item.id())


class Result(unittest.TestResult):
    def __init__(self, names):
        super().__init__()
        self.names = names
        self.outcomes = {}

    def startTest(self, test):
        super().startTest(test)
        self.started = time.monotonic()
        emit({'active': self.names[id(test)]})
        signal.alarm(60)

    def stopTest(self, test):
        signal.alarm(0)
        phase, outcome, xfail, reason = self.outcomes.get(id(test), ('call', 'passed', False, ''))
        emit({'id': self.names[id(test)], 'phase': phase, 'outcome': outcome,
              'xfail': xfail, 'reason': reason, 'seconds': time.monotonic() - self.started})
        super().stopTest(test)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.outcomes[id(test)] = ('call', 'failed', False, self._exc_info_to_string(err, test))

    def addError(self, test, err):
        super().addError(test, err)
        reason = self._exc_info_to_string(err, test)
        if id(test) not in self.names:
            # A class/module setup failure prevents its collected cases starting.
            scope = str(test).split('(', 1)[-1].rstrip(')')
            for ident in self.names.values():
                if scope in ident:
                    emit({'id': ident, 'phase': 'setup', 'outcome': 'failed',
                          'xfail': False, 'reason': reason, 'seconds': 0.0})
        else:
            self.outcomes[id(test)] = ('setup', 'failed', False, reason)

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.outcomes[id(test)] = ('call', 'skipped', False, reason)

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self.outcomes[id(test)] = ('call', 'skipped', True, self._exc_info_to_string(err, test))

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.outcomes[id(test)] = ('call', 'passed', True, '')

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err:
            self.outcomes[id(test)] = ('call' if issubclass(err[0], test.failureException) else 'setup',
                                      'failed', False, self._exc_info_to_string(err, test))


def main():
    suite = load()
    cases = list(flatten(suite))
    counts = Counter(name for _, name in cases)
    occurrences = Counter()
    names = {}
    for test, name in cases:
        occurrences[name] += 1
        names[id(test)] = name + (f'#{occurrences[name]}' if counts[name] > 1 else '')
    if len(names) != len(cases):
        raise ValueError('upstream suite reuses test instances; independent IDs required')
    emit({'collection': list(names.values())})
    if '--collect-only' in sys.argv:
        pytest_sessionfinish(None, 0)
        return 0
    def timeout(signum, frame):
        raise TimeoutError('Timeout (>60s) from corpus')
    signal.signal(signal.SIGALRM, timeout)
    result = Result(names)
    suite.run(result)
    code = int(not result.wasSuccessful())
    pytest_sessionfinish(None, code)
    print(f'Ran {result.testsRun}: failures={len(result.failures)} errors={len(result.errors)} skips={len(result.skipped)}')
    return code


if __name__ == '__main__':
    sys.exit(main())
