"""Exercise scorecard statistics and event windows with recorded observations."""
import json
import os
from pathlib import Path
import runpy
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCORECARD = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/scorecard'))


class ScorecardMeasurements(unittest.TestCase):
    def test_awfy_scores_noisy_iterations_after_fixed_warmup(self):
        rows = SCORECARD['awfy_rows']
        samples = [1000., 900., 800., 1., 2., 3., 4., 5., 6., 7.]
        whole = dict(whole_mean_s=2., whole_stdev_s=.1, whole_median_s=2., whole_min_s=1.9)
        transcript = [f'Bounce: iterations=1 runtime: {int(v * 1e6)}us' for v in samples]
        command = [sys.executable, '-c', 'print(' + repr('\n'.join(transcript + ['Total Runtime:'])) + ')']
        with tempfile.TemporaryDirectory() as scratch, patch.dict(rows.__globals__, {
            'hyperfine': lambda *args, **kwargs: whole,
        }):
            timing = next(rows('go', command, Path(scratch), ['Bounce']))
        self.assertEqual(timing['status'], 'ok')
        self.assertEqual(timing['seconds'], 4.)
        self.assertLess(timing['seconds_low'], timing['seconds'])
        self.assertGreater(timing['seconds_high'], timing['seconds'])
        self.assertEqual(timing['whole_mean_s'], 2.)

    def test_hyperfine_uses_calibrated_prefix_and_exported_wall_clock(self):
        measure = SCORECARD['hyperfine']
        prefix = ['scope wrapper', '--limit']
        candidate = ['binary with space', '-c', 'print("hello!")']
        def recorded(args, **kwargs):
            self.assertEqual(shlex.split(args[args.index('--shell') + 1]), prefix + ['sh'])
            self.assertEqual(shlex.split(args[-1]), ['exec'] + candidate)
            self.assertIn('--ignore-failure=1', args)
            stats = dict(unit="second", count=5, mean=1., median=.99, min=.95, stddev=.05)
            result = dict(measurements=[dict(exit_code=1)] * 5, summary=dict(time_wall_clock=stats))
            Path(args[args.index('--export-json') + 1]).write_text(json.dumps(dict(results=[result])))
        with tempfile.TemporaryDirectory() as scratch, patch('subprocess.run', recorded), \
                patch.dict(os.environ, LUCIFER_PREFIX=shlex.join(prefix)):
            row = measure('lucifer', prefix + candidate, Path(scratch), success_codes=(1,))
        self.assertEqual(row['whole_status'], 'ok')
        self.assertEqual((row['whole_mean_s'], row['whole_stdev_s'], row['whole_median_s'], row['whole_min_s']),
                         (1., .05, .99, .95))
        self.assertLess(row['whole_low_s'], 1.)
        self.assertGreater(row['whole_high_s'], 1.)

    def test_flask_controller_ignores_candidate_python_settings(self):
        measure = SCORECARD['flask_whole']
        def probe(runtime, argv, directory):
            script = argv.index(str(Path(__file__).resolve().parents[2] / 'tools/scorecard'))
            return subprocess.check_output(argv[:script] + ['-c',
                'import os, sys; print(sys.flags.ignore_environment, os.environ["PYTHONHOME"])'],
                env={**os.environ, 'PYTHONHOME': 'candidate-only'}, text=True).strip()

        web = SCORECARD['bench_tool']('web/bench')
        with patch.dict(measure.__globals__, hyperfine=probe):
            output = measure(web, 'cpython', ['candidate'], Path('site'), Path('scratch'), [0], [1], 'digest')
        self.assertEqual(output, '1 candidate-only')

    def test_web_ratio_keeps_completed_run_with_request_errors(self):
        stock = dict(card=2, program='flask.1', runtime='cpython', status='request errors',
                     rps=100., rps_low=90., rps_high=110.)
        candidate = dict(stock, runtime='lucifer', status='ok', rps=50., rps_low=45., rps_high=55.)
        SCORECARD['add_ratios']([stock, candidate])
        self.assertEqual(candidate['rps_over_cpython'], .5)
        self.assertEqual(stock['status'], 'request errors')

    def test_churn_counts_first_build_during_measurement(self):
        events = ['jit-ev 1.1 build cold ms 13.0 : first',
                  'jit-ev 2.1 build late ms 1.5 : first',
                  'jit-ev 2.2 build rebuilt ms 2.5 : guard',
                  'jit-ev 2.3 kill stale',
                  'jit-ev 3.1 kill shutdown']
        counts = SCORECARD['jit_counts'](events, 2., 3.)
        self.assertEqual(counts['post_builds'], 2)
        self.assertEqual(counts['post_kills'], 1)
        self.assertEqual(counts['post_compile_ms'], 4.)
        self.assertEqual(counts['status'], 'churn')
        self.assertEqual(SCORECARD['jit_counts'](events, 4., 5.)['status'], 'ok')

    def test_mean_uses_standard_error_and_student_t(self):
        low, high = SCORECARD["mean_interval"](10, 2, 5)
        self.assertAlmostEqual(high, 12.483, places=3)
        self.assertAlmostEqual(low, 7.517, places=3)

    def test_median_of_identical_values_has_zero_width(self):
        self.assertEqual(SCORECARD["median_interval"]([2.] * 7), (2., 2.))
