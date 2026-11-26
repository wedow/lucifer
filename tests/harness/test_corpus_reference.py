import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
CORPUS = runpy.run_path(str(ROOT / "tools/bench/corpus"))


class CorpusReference(unittest.TestCase):
    def test_refresh_publishes_only_complete_oracle_checks_and_loop_counts(self):
        refresh = CORPUS["refresh"]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "reference.json"
            measured = {"cli.one": dict(status="ok", check="digest", loops=None, seconds=1.2)}
            with patch.dict(refresh.__globals__, measure=lambda *args: measured):
                refresh(Path(temp), ["stock-python"], path)
                expected = {"cli.one": {"check": "digest", "loops": None}}
                self.assertEqual(json.loads(path.read_text()), expected)
                measured["cli.two"] = dict(status="wrong", check=None, loops=None, seconds=None)
                with self.assertRaisesRegex(SystemExit, "cli.two"):
                    refresh(Path(temp), ["stock-python"], path)
                self.assertEqual(json.loads(path.read_text()), expected)


if __name__ == "__main__":
    unittest.main()
