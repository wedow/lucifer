"""Offline binary-level checks for differential result accounting."""
import csv
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

from normalize import normalize
from run import ROOT, HERE
from support import graph


class HarnessChecks(unittest.TestCase):
    def setUp(self):
        (ROOT / '.difftest-work').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='check-', dir=ROOT / '.difftest-work')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cases = {}
        self.add_program('output.py', 'print("correct")\n')
        self.add_program('state.py', 'STATE = {"x": [1, -0.0]}\nprint("correct")\n', state=True)
        self.add_program('uncaught.py', 'raise ValueError("expected")\n', exit=1)

    def add_program(self, name, source, **settings):
        path = self.root / name
        path.write_text(source)
        self.cases[path.relative_to(ROOT).as_posix()] = {
            'sha256': hashlib.sha256(source.encode()).hexdigest(),
            'state': settings.get('state', False), 'timing': settings.get('timing', False),
            'exit': settings.get('exit', 0)}

    def fake(self, mode):
        path = self.root / ('fake_' + mode + '.py')
        path.write_text('''import os, signal, subprocess, sys, time
from pathlib import Path
mode = MODE
if mode == 'startup_fail': sys.exit(17)
if mode == 'startup_hang': time.sleep(20)
if mode == 'startup_malformed': print('not a protocol response'); sys.exit()
if sys.argv[1] == '-c': os.execv(sys.executable, [sys.executable] + sys.argv[1:])
if mode == 'crash': os.kill(os.getpid(), signal.SIGKILL)
if mode == 'timeout': time.sleep(20)
if mode == 'empty': sys.exit()
if mode == 'exception': raise RuntimeError('unexpected target failure')
p = subprocess.run([sys.executable] + sys.argv[1:], capture_output=True)
out, err, code = p.stdout, p.stderr, p.returncode
if mode == 'stdout': out += b'wrong\\n'
if mode == 'stderr': err += b'wrong message\\n'
if mode == 'whitespace': out += b' '
if mode == 'exit': code = 7
if mode == 'state' and sys.argv[-1] != '-': Path(sys.argv[-1]).write_text('{}\\n')
sys.stdout.buffer.write(out); sys.stderr.buffer.write(err)
sys.exit(code)
'''.replace('MODE', repr(mode)))
        return shlex.join([sys.executable, str(path)])

    def run_driver(self, target=None, oracle=None, timeout=2):
        inventory = self.root / 'inventory.json'
        inventory.write_text(json.dumps({'schema': 1, 'cases': self.cases}))
        result = subprocess.run([sys.executable, str(HERE), 'run',
            '--oracle', oracle or sys.executable, '--python', target or sys.executable,
            '--runtime', 'fake', '--inventory', str(inventory), '--results', str(self.root / 'results'),
            '--work', str(self.root / 'work'), '--loops', '2', '--timeout', str(timeout),
            '--startup-timeout', '5', '--allow-unpinned-oracle'],
            capture_output=True, text=True, timeout=30)
        path = self.root / 'results/fake.tsv'
        self.assertTrue(path.exists(), result.stderr)
        raw = path.read_bytes()
        self.assertNotIn(b'\r', raw)
        with path.open(newline='') as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        self.assertEqual([r['id'] for r in rows], sorted(self.cases))
        return result, {Path(row['id']).name: row['status'] for row in rows}

    def test_missing_and_failed_startup_are_all_notrun(self):
        for target in (str(self.root / 'absent'), self.fake('startup_fail'),
                       self.fake('startup_hang'), self.fake('startup_malformed')):
            with self.subTest(target=target):
                result, rows = self.run_driver(target, timeout=0.3)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(set(rows.values()), {'NOTRUN'})

    def test_unavailable_oracle_is_notrun(self):
        _, rows = self.run_driver(oracle=self.fake('startup_fail'))
        self.assertEqual(set(rows.values()), {'NOTRUN'})

    def test_self_diff_including_nonzero_exit(self):
        result, rows = self.run_driver()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(rows.values()), {'PASS'})

    def test_stdout_stderr_whitespace_and_exit_findings(self):
        for mode in ('stdout', 'stderr', 'whitespace', 'exit'):
            with self.subTest(mode=mode):
                _, rows = self.run_driver(self.fake(mode))
                self.assertEqual(set(rows.values()), {'FAIL'})
                self.assertTrue(list((self.root / 'work').rglob('mismatch.diff')))

    def test_crash_timeout_and_empty_output(self):
        for mode, status in (('crash', 'CRASH'), ('timeout', 'TIMEOUT')):
            with self.subTest(mode=mode):
                _, rows = self.run_driver(self.fake(mode), timeout=1)
                self.assertEqual(set(rows.values()), {status})
        _, rows = self.run_driver(self.fake('empty'))
        self.assertEqual(rows['output.py'], 'ERROR')
        self.assertEqual(rows['state.py'], 'ERROR')

    def test_state_is_an_independent_comparison_channel(self):
        _, rows = self.run_driver(self.fake('state'))
        self.assertEqual(rows, {'output.py': 'PASS', 'state.py': 'FAIL', 'uncaught.py': 'PASS'})

    def test_unexpected_exit_is_a_finding_even_without_state(self):
        _, rows = self.run_driver(self.fake('exception'))
        self.assertEqual(rows['state.py'], 'FAIL')
        self.assertEqual(rows['output.py'], 'FAIL')

    def test_crashing_or_hanging_oracle_is_error(self):
        for mode in ('crash', 'timeout'):
            with self.subTest(mode=mode):
                _, rows = self.run_driver(oracle=self.fake(mode), timeout=1)
                self.assertEqual(set(rows.values()), {'ERROR'})

    def test_bad_oracle_is_error(self):
        self.add_program('broken.py', 'raise AssertionError("oracle defect")\n')
        _, rows = self.run_driver()
        self.assertEqual(rows['broken.py'], 'ERROR')

    def test_changed_source_cannot_pass(self):
        (self.root / 'output.py').write_text('print("changed")\n')
        _, rows = self.run_driver()
        self.assertEqual(rows['output.py'], 'ERROR')

    def test_venv_executable_keeps_its_environment(self):
        venv = self.root / 'venv'
        subprocess.run([sys.executable, '-m', 'venv', '--without-pip', str(venv)], check=True, timeout=20)
        executable = venv / 'bin/python'
        site = Path(subprocess.check_output([str(executable), '-c',
            'import sysconfig; print(sysconfig.get_path("purelib"))'], text=True, timeout=10).strip())
        site.mkdir(parents=True, exist_ok=True)
        (site / 'venv_marker.py').write_text('VALUE = "venv dependency"\n')
        self.add_program('dependency.py', 'from venv_marker import VALUE\nprint(VALUE)\n')
        result, rows = self.run_driver(str(executable), oracle=str(executable))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(rows.values()), {'PASS'})

    def test_normalization_is_narrow(self):
        self.assertEqual(normalize(b'<X at 0xabc> 0xabc\n'), b'<X at <ADDRESS>> 0xabc\n')
        self.assertNotEqual(normalize(b'ValueError: a\n'), normalize(b'ValueError: b\n'))
        self.assertNotEqual(normalize(b'x \n'), normalize(b'x\n'))
        self.assertNotEqual(normalize(b'DIFFTEST_TIME=1ns\n'), normalize(b'DIFFTEST_TIME=2ns\n'))
        self.assertEqual(normalize(b'DIFFTEST_TIME=1ns\n', timing=True),
                         normalize(b'DIFFTEST_TIME=2ns\n', timing=True))

    def test_state_retains_aliases_types_and_signed_zero(self):
        shared = []; cycle = [shared, shared]; shared.append(cycle)
        self.assertNotEqual(graph(cycle), graph([[], []]))
        self.assertNotEqual(graph(True), graph(1))
        self.assertNotEqual(graph(1), graph(1.0))
        self.assertNotEqual(graph(0.0), graph(-0.0))
        self.assertNotEqual(graph({'a': 1, 'b': 2}), graph({'b': 2, 'a': 1}))


if __name__ == '__main__':
    unittest.main()
