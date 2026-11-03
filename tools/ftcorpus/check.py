#!/usr/bin/env python3
"""Offline CLI regression checks for the free-threading corpus driver."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


class DriverChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.results = self.root / 'results'
        self.results.mkdir()

    def fixture(self, suite, lane='serial', module='test_module', cases=('test_module.C.test_a', 'test_module.C.test_b')):
        units = {lane + '/' + module: list(cases)}
        if suite == 'ft-packages':
            units = {lane + '/pkg': ['tests/test_x.py::test_a', 'tests/test_x.py::test_b']}
        ids = [(unit.split('/')[0] + '/' + ('pkg/' if suite == 'ft-packages' else '') + case)
               for unit, cases in units.items() for case in cases]
        (self.results / 'inventory.json').write_text(json.dumps({'units': units}))
        (self.results / 'expected.tsv').write_text('id\tstatus\tseconds\n' + ''.join(k + '\tNOTRUN\t0\n' for k in sorted(ids)))
        return ids

    def driver(self, suite, command, *extra):
        return subprocess.run([sys.executable, str(HERE / 'run.py'), suite, 'run', '--python', command,
                               '--runtime', 'fake', '--results', str(self.results), '--startup-timeout', '1',
                               *extra], capture_output=True, text=True, timeout=10)

    def manifest(self):
        with (self.results / 'fake.tsv').open() as stream:
            return list(csv.DictReader(stream, delimiter='\t'))

    def test_unavailable_commands_have_complete_zero_scores(self):
        for suite in ['ft-cpython', 'ft-packages']:
            ids = self.fixture(suite)
            for command in [str(self.root / 'absent'), '/bin/false', '/bin/true',
                            sys.executable + ' -c "import time; time.sleep(2)"']:
                with self.subTest(suite=suite, command=command):
                    result = self.driver(suite, command)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    rows = self.manifest()
                    self.assertEqual([r['id'] for r in rows], sorted(ids))
                    self.assertEqual({r['status'] for r in rows}, {'NOTRUN'})
                    self.assertEqual({float(r['seconds']) for r in rows}, {0})

    def test_gil_enabled_probe_is_not_accepted(self):
        self.fixture('ft-cpython')
        probe = self.root / 'probe'
        probe.write_text('#!/usr/bin/env python3\nimport json\nprint(json.dumps(dict(version=[3,14,10], implementation="cpython", free_threaded=True,gil_enabled=True)))\n')
        probe.chmod(0o755)
        result = self.driver('ft-cpython', str(probe))
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual({r['status'] for r in self.manifest()}, {'NOTRUN'})

    def package_workspace(self):
        import hashlib
        import zipfile
        work = self.root / 'work'
        (work / 'downloads').mkdir(parents=True)
        site = work / 'venv/lib/python3.14t/site-packages'
        site.mkdir(parents=True)
        (site / 'pkg.py').write_text('# pinned payload\n')
        wheel = work / 'downloads/pkg.whl'
        with zipfile.ZipFile(wheel, 'w') as archive:
            archive.write(site / 'pkg.py', 'pkg.py')
        (work / 'tests/pkg').mkdir(parents=True)
        source = work / 'sources/pkg/tests'
        source.mkdir(parents=True)
        (source / 'input.txt').write_text('pristine')
        lock = self.root / 'lock.json'
        lock.write_text(json.dumps({'packages': {'pkg': {'source_dir': 'pkg', 'selectors': ['tests'], 'threads': 4,
                                                       'test_files': ['tests']}},
                                    'artifacts': [{'name': 'pkg', 'filename': 'pkg.whl',
                                                   'sha256': hashlib.sha256(wheel.read_bytes()).hexdigest()}]}))
        return work, lock, site

    def test_package_outcomes_and_watchdogs(self):
        self.fixture('ft-packages')
        work, lock, site = self.package_workspace()
        fake = self.root / 'fake'
        for mode, expected in [('outcomes', {'XFAIL', 'SKIP'}), ('crash', {'CRASH'}),
                               ('hang', {'TIMEOUT'}), ('invalid_id', {'ERROR'}),
                               ('invalid_status', {'ERROR'}), ('fallback', {'ERROR'})]:
            fake.write_text("""#!/usr/bin/env python3
import json,os,pathlib,sys,time,signal
if '-c' in sys.argv:
 print(json.dumps(dict(version=[3,14,10],implementation='cpython',free_threaded=True,gil_enabled=False)))
 sys.exit(0)
mode = MODE
assert pathlib.Path.cwd().parent == pathlib.Path(WORK) / 'runs'
asset = pathlib.Path('tests/input.txt')
assert asset.read_text() == 'pristine'
asset.write_text('modified by a previous attempt')
if mode == 'crash': os.kill(os.getpid(), signal.SIGTERM)
if mode == 'hang': time.sleep(5)
selection = set(json.load(open(os.environ['FT_SELECTION']))) if 'FT_SELECTION' in os.environ else None
with open(os.environ['FT_OBSERVATIONS'],'w') as stream:
 for row in [dict(stage='completion',free_threaded=True,gil_enabled=mode=='fallback'),
             dict(id=[] if mode=='invalid_id' else 'tests/test_x.py::test_a',
                  status={} if mode=='invalid_status' else 'XFAIL',seconds=.1),
             dict(id='tests/test_x.py::test_b',status='SKIP',seconds=.2),dict(exitstatus=0)]:
  if selection is not None and 'id' in row and row['id'] not in selection: continue
  stream.write(json.dumps(row)+'\\n')
""".replace('MODE', repr(mode)).replace('WORK', repr(str(work))))
            fake.chmod(0o755)
            result = self.driver('ft-packages', str(fake), '--lock', str(lock), '--work', str(work), '--timeout', '1')
            self.assertEqual({r['status'] for r in self.manifest()}, expected, result.stderr)
        fake.write_text(fake.read_text().replace("mode = 'fallback'", "mode = 'outcomes'"))
        result = self.driver('ft-packages', str(fake), '--lock', str(lock), '--work', str(work), '--case-shard', '0/2')
        self.assertEqual([r['status'] for r in self.manifest()], ['XFAIL', 'NOTRUN'], result.stderr)
        (site / 'pkg.py').write_text('changed')
        result = self.driver('ft-packages', str(fake), '--lock', str(lock), '--work', str(work))
        self.assertEqual({r['status'] for r in self.manifest()}, {'ERROR'}, result.stderr)

    def test_parallel_lane_scores_serial_fallback_against_oracle(self):
        self.fixture('ft-packages', 'parallel')
        work, lock, _ = self.package_workspace()
        (self.results / 'oracle.json').write_text(json.dumps({'units': {'parallel/pkg': [{'serial_fallback': {}}]}}))
        fake = self.root / 'fake'
        for mode, expected in [('wrapped', ['PASS', 'PASS']), ('serial', ['FAIL', 'FAIL'])]:
            fake.write_text("""#!/usr/bin/env python3
import json,os,sys
if '-c' in sys.argv:
 print(json.dumps(dict(version=[3,14,10],implementation='cpython',free_threaded=True,gil_enabled=False)))
 sys.exit(0)
ids = ['tests/test_x.py::test_a', 'tests/test_x.py::test_b']
with open(os.environ['FT_OBSERVATIONS'],'w') as stream:
 for row in [dict(stage='completion',free_threaded=True,gil_enabled=False)] + [dict(id=i,status='PASS',seconds=.1) for i in ids] + [
         dict(exitstatus=0), dict(parallelism={} if MODE == 'serial' else dict.fromkeys(ids, 4), serial_fallback={})]:
  stream.write(json.dumps(row)+'\\n')
""".replace('MODE', repr(mode)))
            fake.chmod(0o755)
            result = self.driver('ft-packages', str(fake), '--lock', str(lock), '--work', str(work), '--oracle-runtime', 'oracle')
            self.assertEqual([r['status'] for r in self.manifest()], expected, result.stderr)

    def profiled_case_fixture(self, lane):
        profile = json.loads((ROOT / 'results/libtest/resource-profile.json').read_text())['free-threaded']
        module = next(m for m, v in sorted(profile.items()) if len(v['cases']) > 1)
        cases = profile[module]['cases'][:2]
        self.fixture('ft-cpython', lane, module, cases)
        return module, cases

    def test_shell_wrapper_crash_exit_is_crash(self):
        self.profiled_case_fixture('serial')
        wrapper = self.root / 'wrapper'
        wrapper.write_text('''#!/bin/sh
[ "$1" = -c ] && echo '{"version":[3,14,10],"implementation":"cpython","free_threaded":true,"gil_enabled":false}' && exit 0
exit 139
''')
        wrapper.chmod(0o755)
        result = self.driver('ft-cpython', str(wrapper))
        self.assertEqual({r['status'] for r in self.manifest()}, {'CRASH'}, result.stderr)

    def test_cpython_parallel_lane_scores_serial_fallback_against_oracle(self):
        module, ids = self.profiled_case_fixture('parallel')
        fake = self.root / 'fake'
        for oracle_serial, mode, expected in [([], 'wrapped', ['PASS', 'PASS']), ([], 'serial', ['FAIL', 'FAIL']),
                                              (ids[:1], 'serial', ['PASS', 'FAIL'])]:
            (self.results / 'oracle.json').write_text(json.dumps({'units': {'parallel/' + module: [{'serial_cases': oracle_serial}]}}))
            fake.write_text('''#!/usr/bin/env python3
import json,os,pathlib,sys
if '-c' in sys.argv:
 print(json.dumps(dict(version=[3,14,10],implementation='cpython',free_threaded=True,gil_enabled=False)))
 sys.exit(0)
ids = %r
pathlib.Path(os.environ['LIBTEST_STATE']).write_text(json.dumps(dict(module=%r,state='PASSED',seconds=0,errors=[],failures=[]))+'\\n')
with open(os.environ['FT_OBSERVATIONS'],'w') as stream:
 stream.write(json.dumps(dict(free_threaded=True,gil_enabled=False))+'\\n')
 for i in ids: stream.write(json.dumps(dict(id=i,**({'threads':4} if %r == 'wrapped' else {'serial':True})))+'\\n')
with open(os.environ['LIBTEST_EVENTS'],'w') as stream:
 for i in ids: stream.write(json.dumps(dict(id=i,status='PASS',seconds=0))+'\\n')
''' % (ids, module, mode))
            fake.chmod(0o755)
            result = self.driver('ft-cpython', str(fake), '--oracle-runtime', 'oracle')
            self.assertEqual([r['status'] for r in self.manifest()], expected, (mode, result.stderr))

    def test_interpreter_symlink_is_preserved(self):
        self.fixture('ft-cpython')
        actual = self.root / 'actual-target'
        actual.write_text("""#!/usr/bin/env python3
import json,os,pathlib,sys
if not sys.argv[0].endswith('venv-target'):sys.exit(1)
if '-c' in sys.argv:
 print(json.dumps(dict(version=[3,14,10],implementation='cpython',free_threaded=True,gil_enabled=False)))
else:
 pathlib.Path(os.environ['FT_OBSERVATIONS']).write_text(json.dumps(dict(free_threaded=True,gil_enabled=False))+'\\n')
 pathlib.Path(os.environ['LIBTEST_STATE']).write_text(json.dumps(dict(module='test_module',state='PASSED',seconds=0,errors=[],failures=[]))+'\\n')
 with open(os.environ['LIBTEST_EVENTS'],'w') as stream:
  for name in ['test_module.C.test_a','test_module.C.test_b']:
   stream.write(json.dumps(dict(id=name,status='PASS',seconds=0))+'\\n')
""")
        actual.chmod(0o755)
        link = self.root / 'venv-target'
        link.symlink_to(actual)
        result = self.driver('ft-cpython', str(link))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({r['status'] for r in self.manifest()}, {'PASS'})

    def test_collection_keeps_skip_diagnostics_out_of_ids(self):
        import os
        source = self.root / 'source'
        for name in ['Lib/test/libregrtest/tsan.py', '.github/workflows/reusable-san.yml',
                     'Tools/tsan/suppressions.txt', 'Tools/tsan/suppressions_free_threading.txt']:
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('')
        (source / 'Lib/test/libregrtest/tsan.py').write_text(
            "TSAN_TESTS=['test_abc','test_asyncio.test_windows_events']\nTSAN_PARALLEL_TESTS=[]\n")
        bindir = self.root / 'bin'
        bindir.mkdir()
        git = bindir / 'git'
        git.write_text('#!/bin/sh\necho b703b2c5e704341d61e367b9b940b77ed11dded0\n')
        git.chmod(0o755)
        fake = bindir / 'target'
        fake.write_text("""#!/usr/bin/env python3
import json,sys
if '-c' in sys.argv:
 print(json.dumps(dict(version=[3,14,10],implementation='cpython',free_threaded=True,gil_enabled=False)))
elif sys.argv[-1] == 'test_abc': print('case.C.test_ok')
else: print('\\n1 test skipped:\\n    test.test_asyncio.test_windows_events',file=sys.stderr)
""")
        fake.chmod(0o755)
        env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ['PATH'])
        result = subprocess.run([sys.executable, str(HERE / 'run.py'), 'ft-cpython', 'collect',
                                 '--python', str(fake), '--source', str(source), '--results', str(self.results)],
                                env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        with (self.results / 'expected.tsv').open() as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        self.assertEqual([row['id'] for row in rows], ['serial/case.C.test_ok'])

    def test_scoreboard_counts_missing_rows(self):
        self.fixture('ft-cpython')
        (self.results / 'fake.tsv').write_text('id\tstatus\tseconds\nserial/test_module.C.test_a\tPASS\t0.1\n')
        result = subprocess.run([sys.executable, str(ROOT / 'tools/scoreboard'), str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('50.00%', result.stdout)

    def test_parallel_wrapper_retains_skip_and_failure(self):
        # Exercise target observer through a minimal stand-in regrtest package.
        base = self.root / 'lib'
        (base / 'test/libregrtest').mkdir(parents=True)
        for p in ['test/__init__.py', 'test/libregrtest/__init__.py']:
            (base / p).write_text('')
        (base / 'test/libregrtest/parallel_case.py').write_text('''import unittest
class ParallelTestCase(unittest.TestCase):
    def __init__(self, case): self.test_case=case; self.num_threads=4
    def run(self, result):
        result.addSuccess(self)
        result.skipped.append((self.test_case, 'upstream skip'))
        result.failures.append((self.test_case, 'upstream failure'))
''')
        (base / 'test/libregrtest/testresult.py').write_text('from unittest import TestResult as RegressionTestResult\n')
        # Run just the adapter initialization, stopping before the real bridge.
        script = '''import runpy,sys,unittest
real=runpy.run_path
runpy.run_path=lambda *a,**k: None
sys.argv=['adapter','--worker']
exec(compile(open(sys.argv0).read(),sys.argv0,'exec'),{'__name__':'__main__','__file__':sys.argv0})
from test.libregrtest.parallel_case import ParallelTestCase
class Case(unittest.TestCase):
 def test_case(self):pass
ParallelTestCase(Case('test_case')).run(unittest.TestResult())
'''
        script = script.replace("sys.argv0", repr(str(HERE / 'regrtest.py')))
        import os
        env = dict(os.environ, PYTHONPATH=str(base), FT_OBSERVATIONS=str(self.root / 'gil.jsonl'), LIBTEST_EVENTS=str(self.root / 'events.jsonl'))
        result = subprocess.run([sys.executable, '-c', script], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [json.loads(line) for line in (self.root / 'events.jsonl').read_text().splitlines()]
        self.assertEqual({r['status'] for r in events}, {'SKIP', 'FAIL'})
        self.assertTrue(events[0]['id'].endswith('Case.test_case'))


if __name__ == '__main__':
    unittest.main()
