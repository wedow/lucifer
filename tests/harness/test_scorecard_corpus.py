"""Exercise scorecard rows at the boundary with the pinned pyperf worker."""
import hashlib
import importlib.util
from pathlib import Path
import runpy
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
CORPUS_ROWS = runpy.run_path(str(ROOT / "tools/scorecard"))["corpus_rows"]


class CorpusStatistics(unittest.TestCase):
    def test_worker_reports_pyperf_values_without_warmups(self):
        site = ROOT / "build/corpus/site"
        if not (site / "pyperf").is_dir():
            spec = importlib.util.find_spec("pyperf")
            if spec is None:
                self.skipTest("pyperf is available after preparing the benchmark corpus")
            site = Path(spec.origin).parent.parent
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as scratch:
            root = Path(scratch)
            source = root / "bench.py"
            source.write_text("import pyperf\n"
                              "samples = iter([1000] * 3 + [1, 3] * 4)\n"
                              "pyperf.Runner().bench_time_func('noisy', lambda loops: next(samples))\n")
            process = CORPUS_ROWS.__globals__["corpus_process"]
            tasks, _ = process([sys.executable], [str(source)], site, "fixture", root / "run", 1)
        event = tasks["noisy"]
        self.assertEqual(event["values"], [1, 3] * 4)
        self.assertEqual(event["mean"], 2)
        self.assertAlmostEqual(event["stdev"], (8 / 7) ** .5)
        self.assertLessEqual(*event["window"])

    def test_noisy_measurement_and_post_warmup_churn(self):
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as scratch:
            root = Path(scratch)
            stderr = root / "stderr"
            stderr.write_text("")
            reference = {"pyperformance-warm.noisy": {
                "loops": 4, "check": ["noisy", hashlib.sha256(b"").hexdigest()]}}
            inputs = (SimpleNamespace(STARTUP=()), {"noisy": ["bench.py"]}, reference)
            event = dict(values=[1, 3] * 4, ops=4, mean=2, stdev=(8 / 7) ** .5,
                         window=[10, 18])
            log = root / "events"
            log.write_text("jit-ev 9.0 build warm ms 4: first\n"
                           "jit-ev 11.0 build work ms 3: first\n"
                           "jit-ev 20.0 kill after\n")
            run = Mock(side_effect=[({"noisy": event}, stderr), ({"noisy": event}, log)])
            whole = Mock(return_value=dict(whole_mean_s=7, whole_status="ok"))
            with patch.dict(CORPUS_ROWS.__globals__, corpus_inputs=lambda _: inputs,
                            corpus_process=run, corpus_classification=lambda *_: (4, "mixed"),
                            hyperfine=whole):
                rows = list(CORPUS_ROWS("lucifer", ["candidate"], root, root, ["noisy"]))
        timing = next(r for r in rows if r["card"] == 4)
        self.assertEqual(timing["status"], "ok")
        self.assertEqual(timing["seconds"], 2)
        self.assertAlmostEqual(timing["stdev_s"], (8 / 7) ** .5)
        self.assertEqual(timing["whole_mean_s"], 7)
        churn = next(r for r in rows if r["card"] == 5)
        self.assertEqual(churn["status"], "churn")
        self.assertEqual((churn["post_builds"], churn["post_kills"], churn["post_compile_ms"]), (1, 0, 3))


if __name__ == "__main__":
    unittest.main()
