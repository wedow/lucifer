#!/usr/bin/env python3
"""Exercise the README stats command on daily scorecard records."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest

STATS = Path(__file__).resolve().parent / 'stats'
README = '# Title\n\n<!-- stats:begin -->\nold\n<!-- stats:end -->\n\nLicense.\n'
SCORES = [
    ('compute', '1.9× Go time / 1.2× PyPy time', '2/14 Go, 4/14 PyPy'),
    ('held-out', '0.82× stock time', '4/37 paired'),
    ('web', '1 CPU 0.27× / 8 CPUs —', '1/2 layouts'),
    ('short', 'CLI 3.9× / startup 2.2× time', '6/6 CLI, 3/3 startup'),
    ('native', 'majority 2.3× / mixed 1.9× time', '20/20 majority, 27/27 mixed'),
    ('steady', '4/11 settled', '6 unsettled, 1 unavailable'),
]
GATES = [
    ('capi', '70.8%', '1,153 of 1,628', '2026-01-02'),
    ('numpy', '90.0%', '45 of 50 collected', '2025-12-28'),
    ('libtest', '80.0%', "40 of CPython's 50", '2025-12-27'),
    ('kernel', '99.5%', '590 of 593, 4 modes', '2026-01-02'),
    ('differential', '100.0%', '164 of 164, 2 modes', '2026-01-02'),
]


class StatsCheck(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.readme = self.root / 'README.md'
        self.readme.write_text(README)
        older = self.root / 'scorecard-2026-01-01'
        older.mkdir()
        (older / 'summary.csv').write_text('obsolete\n')
        self.day = self.root / 'scorecard-2026-01-02'
        self.day.mkdir()
        with (self.day / 'summary.csv').open('w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['kind', 'program', 'score', 'counts', 'date', 'status'])
            for program, score, counts in reversed(SCORES):
                writer.writerow(['headline', program, score, counts, '', ''])
            for program, score, counts, date in GATES:
                writer.writerow(['gate', program, score, counts, date, 'ok'])
            # Raw programs and command-level gates also live in this file.
            writer.writerow(['detail', 'compute', 'unrelated', '', '', 'ok'])
            writer.writerow(['gate', 'check-capi', 'command result', '', '', 'pass'])

    def update(self):
        subprocess.run([STATS, '--results', self.root, '--readme', self.readme], check=True)
        return self.readme.read_text()

    def test_latest_record_supplies_each_card_and_gate(self):
        result = self.update()
        rows = [line.split(' | ') for line in result.splitlines() if line.startswith('| ')]
        self.assertEqual(len(rows), 12)  # header, six score rows and five correctness rows
        for row, (_, score, counts) in zip(rows[1:7], SCORES):
            self.assertEqual(row[1:], [score, counts, '2026-01-02 |'])
        for row, (_, score, counts, date) in zip(rows[7:], GATES):
            self.assertEqual(row[1:], [score, counts, date + ' |'])
        for index, label in enumerate(['Go / PyPy', 'Held-out', 'Flask', 'Short programs', 'Native', 'steady state'], 1):
            self.assertIn(label, rows[index][0])
        self.assertTrue(result.startswith('# Title\n\n<!-- stats:begin -->\n'))
        self.assertTrue(result.endswith('\n<!-- stats:end -->\n\nLicense.\n'))
        self.assertEqual(self.update(), result)

    def test_missing_scorecard_leaves_readme_intact(self):
        for csv_path in self.root.glob('scorecard-*/summary.csv'):
            csv_path.unlink()
        run = subprocess.run([STATS, '--results', self.root, '--readme', self.readme], capture_output=True, text=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('scorecard', run.stderr)
        self.assertNotIn('Traceback', run.stderr)
        self.assertEqual(self.readme.read_text(), README)


if __name__ == '__main__':
    unittest.main()
