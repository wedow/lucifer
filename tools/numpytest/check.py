#!/usr/bin/env python3
"""Binary-level harness checks, requiring only the host's Python stdlib."""

import base64
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
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
            "if mode=='fail':sys.exit(42)\n"
            "if mode=='badprobe':print('[]');sys.exit()\n"
            "if '-c' in sys.argv:\n"
            " print(json.dumps(dict(numpy='1.0',version='3.14.10',free_threaded_build=True,gil_enabled=False)));sys.exit()\n"
            "ids=" + repr(self.ids) + "\n"
            "shards=json.loads(os.environ['NUMPYTEST_SHARDS'])\n"
            "ids=[i for i in ids if any(i.startswith(s) for s in shards)]\n"
            "if mode=='empty':ids=[]\n"
            "pathlib.Path(os.environ['NUMPYTEST_COLLECTION']).write_text(json.dumps(ids))\n"
            "if mode=='crash':\n"
            " pathlib.Path(os.environ['NUMPYTEST_ACTIVE']).write_text(ids[1])\n"
            " pathlib.Path(os.environ['NUMPYTEST_REPORTS']).write_text(json.dumps(dict(id=ids[0],when='call',outcome='passed',seconds=0.125,wasxfail=False))+'\\n')\n"
            " import signal;os.kill(os.getpid(),signal.SIGKILL)\n"
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
            " if not i and mode=='failcase':E.SubElement(case,'failure',message='assertion failed')\n"
            "E.ElementTree(root).write(sys.argv[sys.argv.index('--junitxml')+1])\n"
            "pathlib.Path(os.environ['NUMPYTEST_STATE']).write_text(json.dumps(dict(gil_enabled=False)))\n"
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
        for mode in ("fail", "badprobe"):
            with self.subTest(mode=mode):
                self.mode(mode)
                self.assertEqual(self.invoke().returncode, 0)
                self.assertTrue(Path(str(self.fake) + ".started").exists())
                self.assertEqual(self.rows(), dict.fromkeys(self.ids, "NOTRUN"))

    def test_junit_preserves_ids_and_xfail(self):
        outcome = self.invoke()
        self.assertEqual(outcome.returncode, 0, outcome.stderr)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["PASS", "XFAIL"])))

    def test_tampered_wheel_payload(self):
        (self.site / "fixture.so").write_bytes(b"changed")
        self.assertEqual(self.invoke().returncode, 0)
        self.assertFalse(Path(str(self.fake) + ".started").exists())
        self.assertEqual(self.rows(), dict.fromkeys(self.ids, "NOTRUN"))

    def test_crash_preserves_completed_journal(self):
        self.mode("crash")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["PASS", "CRASH"])))

    def test_xpass_error_and_failure_are_not_passes(self):
        for mode, status in [("xpass", "XPASS"), ("error", "ERROR"), ("failcase", "FAIL")]:
            with self.subTest(mode=mode):
                self.mode(mode)
                self.invoke()
                self.assertEqual(self.rows()[self.ids[0]], status)

    def test_shard_preserves_other_outcomes(self):
        self.assertEqual(self.invoke().returncode, 0)
        self.mode("failcase")
        self.assertNotEqual(self.invoke("--shard", self.ids[0].split("[")[0]).returncode, 0)
        self.assertEqual(self.rows(), dict(zip(self.ids, ["FAIL", "XFAIL"])))

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
