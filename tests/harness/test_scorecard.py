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
            for name in ("Bounce", "CD"):
                rows.append(dict(card=1, program="awfy." + name, runtime=runtime, status="ok",
                                 seconds=1., seconds_low=.9, seconds_high=1.1, whole_mean_s=2.,
                                 whole_stdev_s=.2, whole_median_s=2., whole_min_s=1.7,
                                 whole_low_s=1.8, whole_high_s=2.2))
                if runtime == "lucifer":
                    builds = int(name == "CD")
                    rows.append(dict(card=5, program="awfy." + name, runtime=runtime,
                                     post_builds=builds, post_kills=0, post_compile_ms=builds * .5,
                                     status="churn" if builds else "ok"))
        rows.extend([
            dict(card=2, program="flask.8", runtime="lucifer", status="insufficient disjoint CPUs"),
            dict(card=2, program="flask.8.shared", runtime="lucifer", status="ok", rps=100),
            dict(card=2, program="flask.8.shared", runtime="cpython", status="ok", rps=50),
            dict(card=2, program="flask.1.profile", runtime="lucifer", status="ok", profile_status="ok",
                 compiled_pct=4),
        ])
        # One candidate verifies two tasks, whose stock and candidate sums give 2x stock time.
        # A second manifest appears only in a third runtime and still belongs in the denominator.
        for runtime, values in (("cpython", (1, 3)), ("lucifer", (6, 2))):
            for task, seconds in enumerate(values):
                rows.append(dict(card=1, program=f"pyperformance.measured/{task}", manifest="measured",
                                 group="held out", runtime=runtime, status="ok", seconds=seconds,
                                 seconds_low=seconds * .9, seconds_high=seconds * 1.1))
        rows.append(dict(card=1, program="pyperformance.missing", manifest="missing", group="held out",
                         runtime="pypy", status="ok", seconds=1))

        def render(date):
            (work / "samples.jsonl").write_text("".join(json.dumps(row) + "\n" for row in rows) + '{"card"')
            out = work / ("scorecard-" + date)
            done = subprocess.run([sys.executable, str(ROOT / "tools/scorecard"), "--summarize", str(work),
                            "--output", str(out), "--gates", str(gates)], capture_output=True, text=True)
            self.assertEqual(done.returncode, 1, done.stderr)
            self.assertTrue((out / "summary.csv").is_file(), done.stderr)
            with (out / "summary.csv").open() as f:
                data = list(csv.DictReader(f))
            return (out / "summary.md").read_text(), data

        md, data = render("2026-01-02")
        heads = {r["program"]: r for r in data if r["kind"] == "headline"}
        self.assertEqual(set(heads), {"compute", "held-out", "web", "short", "native", "churn"})
        self.assertIn("2/14", heads["compute"]["score"])
        self.assertIn("12 failed", heads["compute"]["counts"])
        self.assertIn("(", heads["compute"]["score"])
        self.assertNotIn("Paired / observed", md)
        detail = next(r for r in data if r["kind"] == "detail" and r["card"] == "1"
                      and r["program"] == "awfy.Bounce" and r["runtime"] == "lucifer")
        self.assertLess(float(detail["whole_over_go_low"]), float(detail["whole_over_go"]))
        self.assertGreater(float(detail["whole_over_go_high"]), float(detail["whole_over_go"]))
        self.assertIn("whole_mean_s", md)
        self.assertIn("2×", heads["held-out"]["score"])
        self.assertIn("1/2", heads["held-out"]["score"])
        card = next(r for r in data if r["kind"] == "aggregate" and r["card"] == "1")
        # Two AWFY manifests at 1x and one two-task manifest at 2x.
        self.assertIn("stock 1.26×", card["score"])
        self.assertIn("3/16", card["score"])
        self.assertTrue(any(r["kind"] == "detail" and r["program"] == "awfy.Mandelbrot"
                            and r["runtime"] == "go" and r["status"] != "ok" for r in data))
        self.assertIn("FAIL", heads["churn"]["score"])
        self.assertIn("1/2", heads["churn"]["score"])
        self.assertNotIn("2×", heads["web"]["score"])  # A shared client cannot qualify the disjoint layout.
        self.assertIn("insufficient disjoint CPUs", md)
        self.assertIn("compiled_pct", md)
        self.assertIn("kernel_pct", md)
        self.assertNotIn("unsettled", md)
        positions = [md.index(x) for x in ("## Scorecards", "## 1.", "## 2.", "## 3.", "## 4.", "## 5.",
                                           "## Gates", "## Problems")]
        self.assertEqual(sorted(positions), positions)
        self.assertIn("check-jit", md[md.index("## Problems"):])
        self.assertIn("worker exited 7", md[md.index("## Problems"):])
        self.assertIn("samples.jsonl", md[md.index("## Problems"):])
        self.assertTrue(any(r["kind"] == "gate" and r["program"] == "check-jit" for r in data))

        for row in rows:
            if row["card"] == 1 and row["runtime"] == "lucifer" and row["program"].startswith("awfy."):
                row.update(seconds=1.01, seconds_low=.91, seconds_high=1.11)
        md, data = render("2026-01-03")
        change = next(r["change"] for r in data if r["kind"] == "headline" and r["program"] == "compute")
        self.assertNotIn("%", change)
        for row in rows:
            if row["card"] == 1 and row["runtime"] == "lucifer" and row["program"].startswith("awfy."):
                row.update(seconds=2.02, seconds_low=1.82, seconds_high=2.22)
        md, _ = render("2026-01-04")
        self.assertIn("+100%", md[:md.index("## 1.")])
        md, _ = render("2026-01-06")
        self.assertNotIn("+100%", md[:md.index("## 1.")])


if __name__ == "__main__":
    unittest.main()
