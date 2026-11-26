"""Check the daily's real gate driver against small, deterministic gate commands."""
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ScorecardGatesTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root / 'tools').mkdir()
        for name in ('scorecard-gates', 'scoreboard'):
            shutil.copy(ROOT / 'tools' / name, self.root / 'tools' / name)
        for suite in ('numpytest', 'libtest'):
            folder = self.root / 'results' / suite
            folder.mkdir(parents=True)
            (folder / 'expected.tsv').write_text('id\nalpha\nbeta\n')
            for runtime, second in [('lucifer', 'FAIL'), ('cpython-3.14.10t', 'PASS')]:
                (folder / f'{runtime}.tsv').write_text(
                    f'id\tstatus\tseconds\nalpha\tPASS\t0\nbeta\t{second}\t0\n')
            metadata = 'lucifer-shard.json' if suite == 'numpytest' else 'lucifer.json'
            (folder / metadata).write_text(json.dumps({'recorded_at': '2026-11-15T04:05:06+00:00'}))
        self.prefix = self.root / 'prefix'
        self.prefix.write_text('''#!/usr/bin/env python3
import json, sys
from pathlib import Path
call = sys.argv[1:]
with open('calls.jsonl', 'a') as log:
    print(json.dumps(call), file=log)
if call[0] == 'make':
    target = call[1]
    if target == 'abi-audit':
        print('functions: 4 implemented, 1 stubs, of 5')
    if target == 'check-kernel':
        print('10/10 checks pass (plain)')
    if target == 'check-kernel-jit':
        print('9/10 checks pass (jit)')
        print('10/10 checks pass (jit-deopt)')
        sys.exit(2)
    if target == 'check-jit':
        for name in ('always', 'deopt'):
            folder = Path('build/difftest') / name
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'lucifer.tsv').write_text('id\\tstatus\\tseconds\\na\\tPASS\\t0\\n')
elif '--shard' in call:
    sys.exit(int(call[-1] == 'alpha'))
elif 'tools/libtest/run.py' in call:
    sys.exit(3)
else:
    import os
    os.execvp(call[0], call)
''')
        self.prefix.chmod(0o755)

    def record(self, date):
        path = self.root / 'build' / 'gates.csv'
        env = dict(os.environ, TEST_PREFIX=str(self.prefix), ORACLE_T='oracle',
                   HOST_PYTHON='host-python', NUMPY_SITE='site', NUMPY_CACHE='cache', NUMPY_WORK='raw')
        done = subprocess.run([sys.executable, self.root / 'tools/scorecard-gates',
                               '--date', date, '--output', path], cwd=self.root, env=env,
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        with path.open() as file:
            rows = {r['program']: r for r in csv.DictReader(file)}
        calls = [json.loads(line) for line in (self.root / 'calls.jsonl').read_text().splitlines()]
        return rows, calls

    def test_daily_records_every_gate_and_dates_correctness_counts(self):
        rows, calls = self.record('2026-11-25')
        for target in ('check-fast-float', 'check-blocking', 'check-globals', 'check-capi',
                       'check-kernel', 'check-kernel-jit', 'check-embed', 'check-jit', 'abi-audit'):
            self.assertTrue(any(call[:2] == ['make', target] and 'ORACLE_T=oracle' in call
                                and 'HOST_PYTHON=host-python' in call for call in calls), target)
        self.assertEqual(rows['check-kernel-jit']['status'], 'fail')
        self.assertEqual(rows['kernel']['score'], '90.0%')
        self.assertIn('3 modes', rows['kernel']['counts'])
        self.assertEqual(rows['differential']['score'], '100.0%')
        self.assertEqual(rows['capi']['score'], '80.0%')
        for suite in ('numpy', 'libtest'):
            self.assertEqual(rows[suite]['date'], '2026-11-15')
            self.assertEqual(rows[suite]['score'], '50.0%')
        self.assertFalse(any('--shard' in call or '--source' in call for call in calls))

    def test_saturday_runs_installed_libtest_and_reports_failure(self):
        rows, calls = self.record('2026-11-21')
        self.assertTrue(any('tools/libtest/run.py' in c and 'installed' in c and '--jobs' in c for c in calls))
        self.assertEqual(rows['run-libtest']['status'], 'fail')
        self.assertFalse(any('--shard' in c for c in calls))

    def test_sunday_reports_each_failed_shard_and_finishes(self):
        rows, calls = self.record('2026-11-22')
        self.assertEqual([c[c.index('--shard') + 1] for c in calls if '--shard' in c], ['alpha', 'beta'])
        self.assertEqual(rows['run-numpy']['status'], 'fail')
        self.assertIn('alpha', rows['run-numpy']['detail'])
        self.assertFalse(any('--source' in c for c in calls))


if __name__ == '__main__':
    unittest.main()
