#!/usr/bin/env python3
"""Binary-level checks for tools/readme/stats on fixture status records."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

STATS = Path(__file__).resolve().parent / "stats"

RECORD = """metric,value,previous
awfy.A.on_over_cpython,0.5,0.6
awfy.B.on_over_cpython,2.0,2.1
awfy.geomean_on_over_cpython,1.0,1.1
conformance.kernel.plain.pass,593,580
conformance.kernel.plain.total,593,583
conformance.kernel.jit.pass,590,580
conformance.kernel.jit.total,593,583
conformance.difftest.total,164,164
conformance.difftest.lucifer.pass,153,153
conformance.difftest.always.pass,164,
conformance.difftest.always.total,164,
conformance.difftest.deopt.pass,164,
conformance.difftest.deopt.total,164,
conformance.libtest-core.oracle.pass,2157,
conformance.libtest-core.lucifer.pass,1993,
conformance.libtest-core.date,2025-12-01,
conformance.numpytest.total,52273,52273
conformance.numpytest.lucifer.pass,50299,50299
conformance.numpytest.date,2025-12-28,
capi.implemented,1083,968
capi.total,1628,1628
"""

README = "# Title\n\nIntro.\n\n<!-- stats:begin -->\nold\n<!-- stats:end -->\n\nLicense.\n"

TABLE = """<!-- stats:begin -->
| Metric | Score | Counts |
|---|---|---|
| Speed vs CPython 3.14t | 1.00x CPython's speed | geomean of 2 programs, 2026-01-02 |
| C-API functions implemented | 66.5% | 1,083 of 1,628 |
| numpy test suite passing | 96.2% | 50,299 of 52,273 collected, run 2025-12-28 |
| Core-type `Lib/test` passing, relative to CPython | 92.4% | 1,993 of CPython's 2,157, run 2025-12-01 |
| Kernel programs passing in every mode | 99.5% | 590 of 593, 2 modes |
| Differential programs matching CPython in every JIT mode | 100.0% | 164 of 164, 2 modes |
<!-- stats:end -->"""


class StatsCheck(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.readme = self.root / "README.md"
        self.readme.write_text(README)
        older = self.root / "status-2026-01-01"
        older.mkdir()
        (older / "summary.csv").write_text(RECORD.replace("50299,50299", "1,1"))
        self.day = self.root / "status-2026-01-02"
        self.day.mkdir()
        (self.day / "summary.csv").write_text(RECORD)

    def stats(self):
        subprocess.run([sys.executable, STATS, "--results", self.root, "--readme", self.readme], check=True)
        return self.readme.read_text()

    def test_renders_latest_record_between_markers_only(self):
        self.assertEqual(self.stats(), README.replace("<!-- stats:begin -->\nold\n<!-- stats:end -->", TABLE))

    def test_idempotent(self):
        once = self.stats()
        self.assertEqual(self.stats(), once)

    def test_geomean_takes_every_perf_ratio(self):
        with (self.day / "summary.csv").open("a") as f:
            f.write("pyperformance.C.on_over_cpython,0.125,\n")
        self.assertIn("| 2.00x CPython's speed | geomean of 3 programs, 2026-01-02 |", self.stats())


if __name__ == "__main__":
    unittest.main()
