"""Check the worker environment without running a timed workload."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]


class ScorecardWorker(unittest.TestCase):
    def test_timeout_keeps_the_command_out_of_the_public_result(self):
        rows = runpy.run_path(str(ROOT / 'tools/scorecard'))['awfy_rows']
        command = ['env', 'PRIVATE=secret', 'candidate']
        measure = Mock(side_effect=subprocess.TimeoutExpired(command, 120))
        with tempfile.TemporaryDirectory() as work, patch.dict(rows.__globals__, awfy_samples=measure):
            observations = list(rows('lucifer', ['candidate'], Path(work), ['Bounce']))
        self.assertEqual([r['status'] for r in observations], ['timed out after 120 s'] * 2)

    def test_workloads_use_the_supplied_temporary_directory(self):
        worker = runpy.run_path(str(ROOT / 'tools/scorecard'))['worker']
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            temp = root / 'short'
            temp.mkdir()
            args = SimpleNamespace(site=root, scratch=root, receipt=root / 'rows.jsonl',
                                   runtimes='lucifer', section='awfy')

            def arm(*unused):
                yield dict(card=3, program='socket', runtime='lucifer', status='ok',
                           temp=tempfile.gettempdir())

            with patch.dict(worker.__globals__, arm=arm), patch.dict(os.environ, TMPDIR=str(temp)), \
                    patch.object(tempfile, 'tempdir', None):
                worker(args)
            self.assertEqual(json.loads(args.receipt.read_text())['temp'], str(temp))


if __name__ == '__main__':
    unittest.main()
