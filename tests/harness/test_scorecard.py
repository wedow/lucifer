"""Exercise the daily report using saved measurements, without retiming them."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DailyScorecard(unittest.TestCase):
    def test_daily_report(self):
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as scratch:
            self.check_report(Path(scratch))

    def check_report(self, work):
        (work / "run.json").write_text(json.dumps(dict(revision="abc", issues=["worker exited 7"], elapsed_s={})))
        gates = work / "gates.csv"
        gates.write_text("program,status,score,counts,date,detail\ncheck-jit,fail,,,,exit 7\n")
        rows = []
        for runtime in ("lucifer", "cpython", "pypy", "go"):
            for name in ("a", "b"):
                ok = name == "a" or runtime != "lucifer"
                rows.append(dict(card=1, program="awfy." + name, runtime=runtime,
                                 status="ok" if ok else "unsettled", seconds=1 if ok else None, final_s=1))
                rows.append(dict(card=5, program="awfy." + name, runtime=runtime,
                                 status="ok" if ok else "unsettled"))
        rows.extend([
            dict(card=2, program="flask.8", runtime="lucifer", status="insufficient disjoint CPUs"),
            dict(card=2, program="flask.8.shared", runtime="lucifer", status="ok", rps=100),
            dict(card=2, program="flask.8.shared", runtime="cpython", status="ok", rps=50),
            dict(card=2, program="flask.1.profile", runtime="lucifer", status="ok", profile_status="ok",
                 compiled_pct=4),
        ])
        def render(date):
            (work / "samples.jsonl").write_text("".join(json.dumps(row) + "\n" for row in rows))
            out = work / ("scorecard-" + date)
            subprocess.run([sys.executable, str(ROOT / "tools/scorecard"), "--summarize", str(work),
                            "--output", str(out), "--gates", str(gates)], check=False, capture_output=True)
            with (out / "summary.csv").open() as f:
                data = list(csv.DictReader(f))
            return (out / "summary.md").read_text(), data

        md, data = render("2026-01-02")
        heads = {r["program"]: r for r in data if r["kind"] == "headline"}
        self.assertEqual(set(heads), {"compute", "held-out", "web", "short", "native", "steady"})
        self.assertIn("1/2", heads["compute"]["counts"])
        self.assertIn("1/2", heads["steady"]["score"])
        self.assertNotIn("2×", heads["web"]["score"])  # A shared client cannot qualify the disjoint layout.
        self.assertIn("insufficient disjoint CPUs", md)
        self.assertIn("compiled_pct", md)
        self.assertIn("kernel_pct", md)
        self.assertIn("unsettled", md)
        positions = [md.index(x) for x in ("## Scorecards", "## 1.", "## 2.", "## 3.", "## 4.", "## 5.",
                                           "## Gates", "## Problems")]
        self.assertEqual(sorted(positions), positions)
        self.assertIn("check-jit", md[md.index("## Problems"):])
        self.assertIn("worker exited 7", md[md.index("## Problems"):])
        self.assertTrue(any(r["kind"] == "gate" and r["program"] == "check-jit" for r in data))

        rows[0]["seconds"] = 2
        md, _ = render("2026-01-03")
        self.assertIn("+100%", md[:md.index("## 1.")])
        md, _ = render("2026-01-05")
        self.assertNotIn("+100%", md[:md.index("## 1.")])


if __name__ == "__main__":
    unittest.main()
