#!/usr/bin/env python3
"""Binary-level checks; run this script with the host Python."""
import csv
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).with_name('run.py')


class HarnessChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ids = ['test_fixture.Example.test_' + name for name in ('first', 'second', 'third')]
        (self.root / 'expected.tsv').write_text('id\tstatus\tseconds\n' + ''.join(
            key + '\tNOTRUN\t0\n' for key in self.ids))
        (self.root / 'inventory.json').write_text(json.dumps({
            'free-threaded': {'test_fixture': {'cases': self.ids}}}))

    def run_fake(self, body=None, timeout=0.15):
        fake = self.root / 'fake.py'
        if body is None:
            command = str(self.root / 'absent')
        else:
            fake.write_text(body)
            command = shlex.join([sys.executable, str(fake)])
        completed = subprocess.run([
            sys.executable, str(RUNNER), 'run', '--python', command,
            '--runtime', 'fake', '--results', str(self.root), '--source', 'installed',
            '--startup-timeout', str(timeout), '--timeout', str(timeout),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=40)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        with (self.root / 'fake.tsv').open(newline='') as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        self.assertEqual([row['id'] for row in rows], self.ids)
        self.assertNotIn(b'\r', (self.root / 'fake.tsv').read_bytes())
        return [row['status'] for row in rows]

    def test_missing(self):
        self.assertEqual(self.run_fake(), ['NOTRUN'] * 3)

    def test_startup_exit_one(self):
        self.assertEqual(self.run_fake('raise SystemExit(1)\n'), ['NOTRUN'] * 3)

    def test_startup_hang(self):
        self.assertEqual(self.run_fake('import time; time.sleep(60)\n'), ['NOTRUN'] * 3)

    def fake_probe(self):
        return """import json,os,sys,time
if '-c' in sys.argv:
    print(json.dumps({'version':[3,14,10], 'implementation':'fake', 'free_threaded':True}))
    raise SystemExit(0)
"""

    @unittest.skipUnless(os.name == 'posix', 'process signals require POSIX')
    def test_module_crash(self):
        self.assertEqual(self.run_fake(self.fake_probe() + """
with open(os.environ['LIBTEST_EVENTS'], 'w') as stream:
    stream.write('{partial record')
import signal
os.kill(os.getpid(), signal.SIGKILL)
"""), ['CRASH'] * 3)

    def test_module_timeout(self):
        # A real regrtest timeout is reported by its parent without waiting for
        # the host's extra 30-second emergency bound.
        self.assertEqual(self.run_fake(self.fake_probe() + """
with open(os.environ['LIBTEST_STATE'], 'w') as stream:
    stream.write(json.dumps({'state':'TIMEOUT'}) + '\\n')
"""), ['TIMEOUT'] * 3)

    def test_xml_and_fixture_skip(self):
        body = self.fake_probe() + """
with open(os.environ['LIBTEST_STATE'], 'w') as stream:
    stream.write(json.dumps({'state':'FAILED'}) + '\\n')
with open(sys.argv[sys.argv.index('--junit-xml') + 1], 'w') as stream:
    stream.write('<testsuites><testsuite>'
                 '<testcase name="test_fixture.Example.test_first" time="0.1"/>'
                 '<testcase name="test_fixture.Example.test_second" time="0.2"><output>expected</output></testcase>'
                 '</testsuite></testsuites>')
with open(os.environ['LIBTEST_EVENTS'], 'w') as stream:
    stream.write(json.dumps({'id':'test_fixture.Example.test_first','status':'FAIL','seconds':0.3}) + '\\n')
    stream.write(json.dumps({'id':'test_fixture.Example.test_third','status':'SKIP','seconds':0}) + '\\n')
"""
        self.assertEqual(self.run_fake(body), ['FAIL', 'XFAIL', 'SKIP'])

    def test_fixture_error_is_not_a_pass(self):
        body = self.fake_probe() + """
with open(os.environ['LIBTEST_STATE'], 'w') as stream:
    stream.write(json.dumps({'state':'FAILED'}) + '\\n')
with open(os.environ['LIBTEST_EVENTS'], 'w') as stream:
    stream.write(json.dumps({'id':'setUpClass (test_fixture.Example)','status':'ERROR','seconds':0}) + '\\n')
"""
        self.assertEqual(self.run_fake(body), ['ERROR'] * 3)

    def test_malformed_xml(self):
        self.assertEqual(self.run_fake(self.fake_probe() + """
with open(os.environ['LIBTEST_STATE'], 'w') as stream:
    stream.write(json.dumps({'state':'PASSED'}) + '\\n')
with open(sys.argv[sys.argv.index('--junit-xml') + 1], 'w') as stream:
    stream.write('<partial')
"""), ['ERROR'] * 3)

    def test_shared_ids_keep_worst_module_outcome(self):
        (self.root / 'inventory.json').write_text(json.dumps({'free-threaded': {
            'test_fixture': {'cases': self.ids},
            'test_fixture_other': {'cases': self.ids},
        }}))
        body = self.fake_probe() + """
with open(os.environ['LIBTEST_STATE'], 'w') as stream:
    stream.write(json.dumps({'state':'PASSED'}) + '\\n')
with open(os.environ['LIBTEST_EVENTS'], 'w') as stream:
    for name in ('first', 'second', 'third'):
        stream.write(json.dumps({'id':'test_fixture.Example.test_' + name,
            'status':'FAIL' if sys.argv[-1].endswith('other') else 'PASS', 'seconds':0}) + '\\n')
"""
        self.assertEqual(self.run_fake(body), ['FAIL'] * 3)

    def test_collection_preserves_upstream_ids(self):
        fake = self.root / 'collector.py'
        fake.write_text(self.fake_probe().replace("'fake'", "'cpython'") + """
if '--list-tests' in sys.argv:
    print('test_fixture')
else:
    print('test_fixture.Example[param space].test_first')
    print('decimal')
    print('unittest.loader.ModuleSkipped.test_optional')
    print('1 test skipped:', file=sys.stderr)
    print('    test_optional', file=sys.stderr)
""")
        completed = subprocess.run([
            sys.executable, str(RUNNER), 'collect', '--python',
            shlex.join([sys.executable, str(fake)]), '--source', 'installed',
            '--results', str(self.root),
        ], capture_output=True, text=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        with (self.root / 'expected.tsv').open() as stream:
            ids = [row['id'] for row in csv.DictReader(stream, delimiter='\t')]
        self.assertEqual(ids, ['decimal', 'test_fixture.Example[param space].test_first',
                               'unittest.loader.ModuleSkipped.test_optional'])


if __name__ == '__main__':
    unittest.main()
