#!/usr/bin/env python3
"""Binary-level harness checks, requiring only the host's Python stdlib."""

import base64
import csv
import hashlib
import json
import io
from pathlib import Path
import shutil
import runpy
import subprocess
import sys
import tarfile
import types
import tempfile
import unittest
from unittest.mock import patch
import zipfile

HERE = Path(__file__).resolve().parent


class HarnessChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="numpytest-check-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tool = self.root / "tools/numpytest"
        self.tool.mkdir(parents=True)
        shutil.copyfile(HERE / "run.py", self.tool / "run.py")
        (self.root / "tools/lib").mkdir()
        shutil.copyfile(HERE.parent / "lib/exitcode.py", self.root / "tools/lib/exitcode.py")
        self.site = self.root / "site"
        self.site.mkdir()
        (self.site / "fixture.so").write_bytes(b"unmodified")
        self.cache = self.root / "cache"
        self.cache.mkdir()
        wheel = self.cache / "fixture.whl"
        with zipfile.ZipFile(wheel, "w") as archive:
            archive.write(self.site / "fixture.so", "fixture.so")
        pin = {"version": "1.0", "wheels": {"cp314t": {
            "filename": wheel.name, "sha256": hashlib.sha256(wheel.read_bytes()).hexdigest()}}}
        (self.tool / "PIN").write_text(json.dumps(pin))
        self.ids = ['numpy/tests/test_fixture.py::test_a[x.y<&"Ω]', "numpy/tests/test_fixture.py::test_b"]
        directory = self.root / "results/numpytest"
        directory.mkdir(parents=True)
        self.expected = directory / "expected.tsv"
        self.expected.write_text("id\tstatus\tseconds\n" + "".join(i + "\tNOTRUN\t0\n" for i in self.ids))
        self.fake = self.root / "fake"
        self.fake.write_text(
            "#!" + sys.executable + "\n"
            "import json,os,pathlib,sys,time\n"
            "pathlib.Path(__file__+'.started').touch()\n"
            "mode=pathlib.Path(__file__+'.mode').read_text()\n"
            "if mode=='pass':assert os.environ['NUMPYTEST_SITE'] not in os.environ['PYTHONPATH'].split(os.pathsep)\n"
            "if mode=='pass':assert not os.environ['PYTHONPYCACHEPREFIX'].startswith(os.environ['NUMPYTEST_SITE'])\n"
            "if mode=='fail':sys.exit(42)\n"
            "if mode=='badprobe':print('[]');sys.exit()\n"
            "if mode=='wrongsite':print(json.dumps(dict(numpy='1.0',numpy_file='/different/numpy/__init__.py')));sys.exit()\n"
            "if '-c' in sys.argv:\n"
            " print(json.dumps(dict(numpy='1.0',numpy_file=os.environ['NUMPYTEST_SITE']+'/numpy/__init__.py',version='3.14.10',free_threaded_build=True,gil_enabled=False)));sys.exit()\n"
            "ids=" + repr(self.ids) + "\n"
            "shards=json.loads(os.environ['NUMPYTEST_SHARDS'])\n"
            "ids=[i for i in ids if any(i.startswith(s) for s in shards)]\n"
            "if mode=='empty':ids=[]\n"
            "pathlib.Path(os.environ['NUMPYTEST_COLLECTION']).write_text(json.dumps(ids))\n"
            "if mode=='crash':\n"
            " pathlib.Path(os.environ['NUMPYTEST_ACTIVE']).write_text(ids[1])\n"
            " pathlib.Path(os.environ['NUMPYTEST_REPORTS']).write_text(json.dumps(dict(id=ids[0],when='call',outcome='passed',seconds=0.125,wasxfail=False))+'\\n')\n"
            " import signal;os.kill(os.getpid(),signal.SIGKILL)\n"
            "if mode=='resume':\n"
            " skip=pathlib.Path(os.environ['NUMPYTEST_SKIP'])\n"
            " done=skip.read_text().split() if skip.exists() else []\n"
            " if not done:\n"
            "  pathlib.Path(os.environ['NUMPYTEST_ACTIVE']).write_text(ids[0])\n"
            "  import signal;os.kill(os.getpid(),signal.SIGSEGV)\n"
            " ids=[i for i in ids if i not in done]\n"
            "if mode=='timeout':\n"
            " pathlib.Path(os.environ['NUMPYTEST_ACTIVE']).write_text(ids[0]);time.sleep(10)\n"
            "import base64,xml.etree.ElementTree as E\n"
            "root=E.Element('testsuite')\n"
            "for i,node in enumerate(ids):\n"
            " case=E.SubElement(root,'testcase',name='deliberately.not.a.nodeid',time='0.125')\n"
            " props=E.SubElement(case,'properties')\n"
            " E.SubElement(props,'property',name='nodeid64',value=base64.b64encode(node.encode()).decode())\n"
            " if i:E.SubElement(case,'skipped',type='pytest.xfail',message='known upstream issue')\n"
            " if not i and mode=='xpass':E.SubElement(props,'property',name='outcome',value='XPASS')\n"
            " if not i and mode=='error':E.SubElement(case,'error',message='setup failed')\n"
            " if not i and mode=='threadwarn' and 'error::pytest.PytestUnhandledThreadExceptionWarning' in sys.argv:E.SubElement(case,'error',message='unhandled thread exception')\n"
            " if not i and mode in ('failcase','badstate'):E.SubElement(case,'failure',message='assertion failed')\n"
            "E.ElementTree(root).write(sys.argv[sys.argv.index('--junitxml')+1])\n"
            "pathlib.Path(os.environ['NUMPYTEST_STATE']).write_text('{' if mode=='badstate' else json.dumps(dict(gil_enabled=False,parallel_ids=[] if mode=='serialonly' else ids)))\n"
        )
        self.fake.chmod(0o755)
        self.mode("pass")

    def mode(self, mode):
        Path(str(self.fake) + ".mode").write_text(mode)

    def invoke(self, *extra, python=None):
        return subprocess.run([sys.executable, str(self.tool / "run.py"), "run",
                               "--python", str(python or self.fake), "--runtime", "fake",
                               "--site", str(self.site), "--cache", str(self.cache),
                               "--work", str(self.root / "raw"), *extra],
                              capture_output=True, text=True)

    def rows(self):
        with (self.expected.parent / "fake.tsv").open() as stream:
            return {r["id"]: r["status"] for r in csv.DictReader(stream, delimiter="\t", quoting=csv.QUOTE_NONE)}

    def test_absent_interpreter(self):
        self.assertEqual(self.invoke(python=self.root / "missing").returncode, 0)
        self.assertEqual(self.rows(), dict.fromkeys(self.ids, "NOTRUN"))

    def test_failing_interpreter(self):
        for mode in ("fail", "badprobe", "wrongsite"):
            with self.subTest(mode=mode):
                self.mode(mode)
                self.assertEqual(self.invoke().returncode, 0)
                self.assertTrue(Path(str(self.fake) + ".started").exists())
                self.assertEqual(self.rows(), dict.fromkeys(self.ids, "NOTRUN"))

    def test_junit_preserves_ids_and_xfail(self):
        outcome = self.invoke()
        self.assertEqual(outcome.returncode, 0, outcome.stderr)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["PASS", "XFAIL"])))

    def test_pinned_documentation_fixtures_and_tampering(self):
        archive = self.cache / 'source.tar.gz'
        with tarfile.open(archive, 'w:gz') as package:
            member = tarfile.TarInfo('numpy-1.0/doc/source/f2py/code/example.f')
            member.size = 7
            package.addfile(member, io.BytesIO(b'example'))
        pinfile = self.tool / 'PIN'
        pin = json.loads(pinfile.read_text())
        pin['sdist'] = dict(filename=archive.name, sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
        pinfile.write_text(json.dumps(pin))
        functions = runpy.run_path(str(self.tool / 'run.py'))
        info = functions['prepare_docs'](self.cache, self.site)
        self.assertEqual(functions['verify_docs'](self.site), info)
        self.assertEqual((self.site / 'fixture.so').read_bytes(), b'unmodified')
        (self.site / 'doc/source/f2py/code/example.f').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'source mismatch'):
            functions['verify_docs'](self.site)
        archive.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'archive hash mismatch'):
            functions['prepare_docs'](self.cache, self.site)

    def test_typing_relocation_preserves_error_and_original_fixture(self):
        source = self.root / 'data'
        (source / 'pass').mkdir(parents=True)
        fixture = source / 'pass/example.py'
        fixture.write_text('invalid fixture\n')
        def fake_run(args):
            copied = Path(args[-1]) / 'example.py'
            self.assertNotEqual(copied, fixture)
            self.assertEqual(copied.read_bytes(), fixture.read_bytes())
            return str(copied) + ':1: error: sentinel\n', '', 1
        module = types.SimpleNamespace(DATA_DIR=str(source), api=types.SimpleNamespace(run=fake_run))
        stub = types.SimpleNamespace(hookimpl=lambda **kw: lambda function: function)
        with patch.dict(sys.modules, pytest=stub):
            functions = runpy.run_path(str(HERE / 'recorder.py'))
        info = functions['relocate_typing_inputs'](module, self.root / 'work')
        self.assertEqual(info['files'], 1)
        self.assertEqual(module.api.run([str(source / 'pass')]), (str(fixture) + ':1: error: sentinel\n', '', 1))
        self.assertEqual(fixture.read_text(), 'invalid fixture\n')

    def test_tampered_wheel_payload(self):
        (self.site / "fixture.so").write_bytes(b"changed")
        self.assertEqual(self.invoke().returncode, 0)
        self.assertFalse(Path(str(self.fake) + ".started").exists())
        self.assertEqual(self.rows(), dict.fromkeys(self.ids, "NOTRUN"))

    def test_crash_preserves_completed_journal(self):
        self.mode("crash")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["PASS", "CRASH"])))

    def test_crash_restarts_without_the_crashed_test(self):
        self.mode("resume")
        self.assertEqual(self.invoke().returncode, 1)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["CRASH", "PASS"])))

    def test_runall_shards_cover_each_id_once(self):
        shard_groups = runpy.run_path(str(HERE / "runall.py"))["shard_groups"]
        ids = [f"numpy/a/test_{n}.py::{c}::t[{i}]" for n in range(5) for c in "XY" for i in range(7)]
        ids += ["numpy/b/test_big.py::test_f[%d]" % i for i in range(30)] + ["numpy/b/test_big.py::C::t"]
        groups = shard_groups(ids, 20)
        for ident in ids:
            hits = [s for g in groups for s in g if (
                ident == s or ident.startswith(s + "::") or ident.startswith(s + "[") if "::" in s
                else ident.split("::", 1)[0] == s)]
            self.assertEqual(len(hits), 1, ident)
        self.assertIn("numpy/b/test_big.py::test_f", [s for g in groups for s in g])

    def test_shell_wrapper_crash_exit_is_crash(self):
        wrapper = self.root / "wrapper"
        wrapper.write_text('#!/bin/sh\n"' + str(self.fake) + '" "$@"\n')
        wrapper.chmod(0o755)
        self.mode("crash")
        self.assertNotEqual(self.invoke(python=wrapper).returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["PASS", "CRASH"])))

    def test_parallel_lane_scores_serial_fallback_against_oracle(self):
        lane = self.root / "results/numpytest-parallel"
        lane.mkdir()
        (lane / "expected.tsv").write_text(self.expected.read_text())
        def run(mode, oracle_fallback):
            (lane / "oracle.json").write_text(json.dumps({"finish": {"serial_fallback": oracle_fallback}}))
            self.mode(mode)
            self.invoke("--parallel", "--oracle-runtime", "oracle")
            with (lane / "fake.tsv").open() as stream:
                return {r["id"]: r["status"] for r in csv.DictReader(stream, delimiter="\t", quoting=csv.QUOTE_NONE)}
        self.assertEqual(run("pass", {}), dict(zip(self.ids, ["PASS", "XFAIL"])))
        self.assertEqual(run("serialonly", {}), dict(zip(self.ids, ["FAIL", "XFAIL"])))
        self.assertEqual(run("serialonly", {self.ids[0]: "thread-unsafe"}), dict(zip(self.ids, ["PASS", "XFAIL"])))

    def test_xpass_error_and_failure_are_not_passes(self):
        for mode, status in [("xpass", "XPASS"), ("error", "ERROR"), ("failcase", "FAIL"), ("threadwarn", "ERROR")]:
            with self.subTest(mode=mode):
                self.mode(mode)
                self.invoke()
                self.assertEqual(self.rows()[self.ids[0]], status)

    def test_shard_preserves_other_outcomes(self):
        self.assertEqual(self.invoke().returncode, 0)
        self.mode("failcase")
        self.assertNotEqual(self.invoke("--shard", self.ids[0].split("[")[0]).returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["FAIL", "XFAIL"])))

    def test_refused_run_clears_previous_outcomes(self):
        self.assertEqual(self.invoke().returncode, 0)
        self.mode('badstate')
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertNotIn('PASS', self.rows().values())

    def test_failing_startup_clears_unselected_shard_results(self):
        self.assertEqual(self.invoke().returncode, 0)
        self.mode('fail')
        self.assertEqual(self.invoke('--shard', self.ids[0].split('[')[0]).returncode, 0)
        self.assertEqual(self.rows(), dict.fromkeys(self.ids, 'NOTRUN'))

    def test_empty_collection_does_not_replace_denominator(self):
        self.mode("empty")
        old = self.expected.read_bytes()
        self.assertNotEqual(self.invoke("--collect").returncode, 0)
        self.assertEqual(self.expected.read_bytes(), old)

    def test_timeout_retains_denominator(self):
        self.mode("timeout")
        self.assertNotEqual(self.invoke("--timeout", "1").returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["TIMEOUT", "NOTRUN"])))

    def test_scoreboard_uses_expected_for_missing_rows(self):
        self.invoke()
        (self.expected.parent / "fake.tsv").write_text(
            "id\tstatus\tseconds\n" + self.ids[0] + "\tPASS\t0.1\n")
        result = subprocess.run([sys.executable, str(HERE.parent / "scoreboard"),
                                 "--results", str(self.root / "results")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("50.00%", result.stdout)
        self.assertIn("\t1\t50.00%", result.stdout)


if __name__ == "__main__":
    unittest.main()
