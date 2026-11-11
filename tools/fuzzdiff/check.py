"""Offline checks for the differential fuzzer: comparison rules, and a run of a fake generator on a
target that generates, computes, crashes and hangs differently on known cases."""
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

from compare import compare
from run import Interpreter, fuzz

GENERATOR = '''import os, time
import common

def gen_case(rng):
    if os.environ.get("FAKE_TARGET") and len(CASES) == 7:
        rng.random()  # the target generates case 7 differently; later cases must stay aligned
    DRAWS.append(0)
    if len(DRAWS) % 4 == 0:
        return None
    CASES.append(0)
    return {"n": len(CASES) - 1, "fn": "f", "r": rng.randrange(1000)}

DRAWS, CASES = [], []

def run_python(c):
    n, target = c["n"], os.environ.get("FAKE_TARGET")
    if n % 11 == 5:
        raise NameError("name 'frozendict' is not defined")
    if n == 29 and not target:
        os.abort()
    if target and n == 17:
        os.abort()
    if target and n == 25:
        time.sleep(30)
    return {"ok": n + (target is not None and n % 10 == 3), "id": id(object())}

if __name__ == "__main__":
    common.main(gen_case, run_python)
'''


class Comparison(unittest.TestCase):
    def test_results_and_exceptions(self):
        self.assertEqual(compare([{}, {'ok': [1, 2]}], [{}, {'ok': [1, 2]}]), 'match')
        self.assertEqual(compare([{}, {'ok': 1}], [{}, {'ok': True}]), 'mismatch')
        self.assertEqual(compare([{}, {'ok': -0.0}], [{}, {'ok': 0.0}]), 'mismatch')
        self.assertEqual(compare([{}, {'exc': 'ValueError', 'msg': 'bad'}],
                                 [{}, {'exc': 'ValueError', 'msg': 'worse'}]), 'mismatch')
        self.assertEqual(compare([{'n': 1}, {'ok': 1}], [{'n': 2}, {'ok': 1}]), 'mismatch')

    def test_addresses_and_values_that_vary_between_oracle_runs(self):
        self.assertEqual(compare([{}, {'ok': '<A object at 0x7f00>'}], [{}, {'ok': '<A object at 0x5a>'}]),
                         'match')
        self.assertEqual(compare([{}, {'ok': '0x7f00'}], [{}, {'ok': '0x5a'}]), 'mismatch')
        want, again = [{}, {'ok': [1, 7], 'id': 10}], [{}, {'ok': [1, 7], 'id': 20}]
        self.assertEqual(compare(want, [{}, {'ok': [1, 7], 'id': 30}], again), 'match')
        self.assertEqual(compare(want, [{}, {'ok': [1, 8], 'id': 30}], again), 'mismatch')
        self.assertEqual(compare(want, [{}, {'ok': [1, 7]}], again), 'mismatch')

    def test_oracle_without_result(self):
        self.assertEqual(compare([{}, {'exc': 'NameError', 'msg': "name 'frozendict' is not defined"}],
                                 [{}, {'ok': 1}]), 'skip')
        self.assertEqual(compare([{}, {'crash': 'exit -6'}], [{}, {'ok': 1}]), 'error')
        self.assertEqual(compare([{}, {'ok': 1}], [None, {'crash': 'exit -6'}]), 'mismatch')


class Run(unittest.TestCase):
    def test_batches_crashes_and_timeouts(self):
        with tempfile.TemporaryDirectory() as temp:
            difftest = Path(temp)
            (difftest / 'common.py').write_text('def main(gen_case, run_python):\n    pass\n')
            (difftest / 'fake_diff.py').write_text(GENERATOR)
            oracle = Interpreter([sys.executable], difftest, difftest, 2, 4 << 30)
            target = Interpreter(['env', 'FAKE_TARGET=1', sys.executable], difftest, difftest, 2, 4 << 30)
            log = io.StringIO()
            counts = fuzz('fake_diff', 0, 30, 8, oracle, target, difftest / 'scratch', log)
        self.assertEqual({key: counts[key] for key in ('cases', 'match', 'mismatch', 'skip', 'error')},
                         {'cases': 30, 'match': 20, 'mismatch': 6, 'skip': 3, 'error': 1})
        self.assertEqual(counts['dropped'], {'none': 9, 'future': 0, 'error': 0})
        records = {record['index']: record for record in map(json.loads, log.getvalue().splitlines())}
        self.assertEqual(sorted(records), [3, 5, 7, 13, 16, 17, 23, 25, 27, 29])
        self.assertNotEqual(records[7]['target_case']['r'], records[7]['case']['r'])
        self.assertEqual(records[13]['oracle']['ok'], 13)
        self.assertEqual(records[13]['target']['ok'], 14)
        self.assertEqual(records[17]['target']['crash'], 'exit -6')
        self.assertEqual(records[25]['target'], {'timeout': 2})
        self.assertEqual(records[29]['status'], 'error')


if __name__ == '__main__':
    unittest.main()
