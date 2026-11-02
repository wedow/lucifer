#!/usr/bin/env python3
"""Offline checks of the shipped command interfaces; no compiler or network needed."""

import csv
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "tools/oracle/build-cpython"
CHECK = ROOT / "tools/oracle/check"
SCOREBOARD = ROOT / "tools/scoreboard"
COMMIT = "b703b2c5e704341d61e367b9b940b77ed11dded0"
REPOSITORY = "https://github.com/python/cpython"

# Minimal executable toolchain records the builder's process boundary contract.
TOOLCHAIN = r'''
import json, os, pathlib, shutil, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["TRACE"], "a") as stream:
    stream.write(json.dumps([name, args, os.environ.get("CFLAGS")]) + "\n")
commit = "b703b2c5e704341d61e367b9b940b77ed11dded0"
if name == "cc":
    print("test compiler 1.0")
elif name == "git":
    if args[0] == "ls-remote":
        print(os.environ.get("FAKE_COMMIT", commit) + "\trefs/tags/v3.14.10^{}")
    elif args[0] == "clone":
        source = pathlib.Path(args[-1])
        source.mkdir()
        shutil.copyfile(__file__, source / "configure")
        (source / "configure").chmod(0o755)
    elif args[0] == "-C":
        if args[2] == "remote":
            print("https://github.com/python/cpython")
        elif args[2] == "rev-parse":
            print(os.environ.get("FAKE_HEAD", commit))
elif name == "configure":
    prefix = next(a.split("=", 1)[1] for a in args if a.startswith("--prefix="))
    pathlib.Path("state.json").write_text(json.dumps([prefix, "--disable-gil" in args]))
    pathlib.Path("Makefile").write_text("test toolchain\n")
elif name == "make":
    if os.environ.get("FAKE_MAKE_FAIL"):
        sys.exit(8)
    destination = next((a.split("=", 1)[1] for a in args if a.startswith("DESTDIR=")), None)
    if destination:
        prefix, disabled = json.loads(pathlib.Path("state.json").read_text())
        directory = pathlib.Path(destination + prefix) / "bin"
        directory.mkdir(parents=True, exist_ok=True)
        target = directory / ("python3.14t" if disabled else "python3.14")
        shutil.copyfile(__file__, target)
        target.chmod(0o755)
        if os.environ.get("FAKE_INSTALL_FAIL"):
            sys.exit(8)
'''


class Commands(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="oracle-harness-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def invoke(self, command, **kwargs):
        return subprocess.run([str(p) for p in command], capture_output=True,
                              text=True, timeout=20, **kwargs)

    def executable(self, path, source):
        path.write_text(f"#!{sys.executable}\n" + textwrap.dedent(source), encoding="utf-8")
        path.chmod(0o755)
        return path


class BuilderTests(Commands):
    def setUp(self):
        super().setUp()
        binary = self.directory / "toolchain"
        binary.mkdir()
        for name in ("git", "cc", "make"):
            self.executable(binary / name, TOOLCHAIN)
        self.trace = self.directory / "trace.jsonl"
        self.environment = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                                TRACE=str(self.trace), CC="cc", JOBS="2", CPPFLAGS="", LDFLAGS="")
        self.environment.pop("ORACLE_WORKDIR", None)
        self.prefix = self.directory / "prefix with ' quote"

    def build(self, *flags):
        return self.invoke([BUILD, "v3.14.10", self.prefix, *flags], env=self.environment)

    def calls(self):
        return [json.loads(line) for line in self.trace.read_text().splitlines()]

    def test_build_publish_and_repeat_both_abis(self):
        for suffix, flags in (("", ()), ("t", ("--disable-gil",))):
            with self.subTest(suffix=suffix):
                self.prefix = self.directory / ("prefix with ' quote" + suffix)
                result = self.build(*flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((self.prefix / ("bin/python3.14" + suffix)).is_file())
                info = (self.prefix / "ORACLE-INFO").read_text()
                self.assertIn("TAG=v3.14.10\nCOMMIT=" + COMMIT, info)
                line = next(line for line in info.splitlines() if line.startswith("CONFIGURE_COMMAND="))
                words = shlex.split(line.split("=", 1)[1])
                self.assertIn("--prefix=" + str(self.prefix), words)
                self.assertIn("--enable-optimizations=no", words)
                self.assertIn("CFLAGS=-O2", words)
                self.assertEqual("--disable-gil" in words, bool(suffix))
                calls = self.calls()
                clone = next(call for call in calls if call[0] == "git" and call[1][0] == "clone")
                self.assertEqual(clone[1][1:6], ["--depth", "1", "--single-branch", "--branch", "v3.14.10"])
                self.assertIn(REPOSITORY, clone[1])
                makes = sum(call[0] == "make" for call in calls)
                repeat = self.build(*flags)
                self.assertEqual(repeat.returncode, 0, repeat.stderr)
                self.assertIn("already installed", repeat.stdout)
                self.assertEqual(makes, sum(call[0] == "make" for call in self.calls()))

    def test_reject_moved_release_tag(self):
        self.environment["FAKE_COMMIT"] = "1" * 40
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pinned commit", result.stderr)
        self.assertFalse(self.prefix.exists())
        self.assertFalse(any(call[0] == "make" for call in self.calls()))

    def test_rebuild_reconfigures_and_preserves_install_on_failure(self):
        self.assertEqual(self.build().returncode, 0)
        binary = self.prefix / 'bin/python3.14'
        original = binary.read_bytes()
        self.environment['FAKE_INSTALL_FAIL'] = '1'
        self.assertNotEqual(self.build('--rebuild').returncode, 0)
        self.assertEqual(binary.read_bytes(), original)
        del self.environment['FAKE_INSTALL_FAIL']
        result = self.build('--rebuild')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sum(call[0] == 'configure' for call in self.calls()), 3)

    def test_jit_configuration(self):
        result = self.build('--enable-experimental-jit')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--enable-experimental-jit=yes', (self.prefix / 'ORACLE-INFO').read_text())
        self.assertNotEqual(self.build('--enable-experimental-jit', '--disable-gil').returncode, 0)

    def test_reject_checkout_mismatch(self):
        self.environment["FAKE_HEAD"] = "2" * 40
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("HEAD differs", result.stderr)
        self.assertFalse(self.prefix.exists())

    def test_failed_build_is_not_published_and_can_resume(self):
        self.environment["FAKE_MAKE_FAIL"] = "1"
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.prefix.exists())
        del self.environment["FAKE_MAKE_FAIL"]
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sum(call[0] == "configure" for call in self.calls()), 1)

    def test_preserve_unrelated_prefix(self):
        self.prefix.mkdir()
        sentinel = self.prefix / "keep"
        sentinel.write_text("keep")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_failed_install_is_not_published_and_can_resume(self):
        self.environment["FAKE_INSTALL_FAIL"] = "1"
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.prefix.exists())
        self.assertTrue(list((self.directory / ".oracle-stage").rglob("python3.14")))
        del self.environment["FAKE_INSTALL_FAIL"]
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_custom_work_directory(self):
        work = self.directory / "custom work"
        self.environment["ORACLE_WORKDIR"] = str(work)
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((work / "source/configure").exists())
        self.assertTrue((work / "build/Makefile").exists())
        self.assertTrue((self.prefix / "ORACLE-INFO").exists())

    def test_reject_changed_build_configuration(self):
        self.assertEqual(self.build().returncode, 0)
        before = (self.prefix / "ORACLE-INFO").read_bytes()
        result = self.build("--disable-gil")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("provenance differs", result.stderr)
        self.assertEqual((self.prefix / "ORACLE-INFO").read_bytes(), before)

    def test_venv_uses_free_threaded_binary_and_preserves_existing_dir(self):
        self.assertEqual(self.build("--disable-gil").returncode, 0)
        destination = self.directory / "environment"
        command = [ROOT / "tools/oracle/venv", self.prefix, destination]
        result = self.invoke(command, env=self.environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[-1][:2], ["python3.14t", ["-I", "-m", "venv", str(destination)]])
        destination.mkdir()
        self.assertNotEqual(self.invoke(command, env=self.environment).returncode, 0)


class CheckTests(Commands):
    def check(self, interpreter, timeout="1"):
        return self.invoke([CHECK, "--python", interpreter, "--runtime", "candidate",
                            "--results", self.directory / "results", "--timeout", timeout])

    def manifest(self):
        path = self.directory / "results/oracle/candidate.tsv"
        with path.open(newline="") as stream:
            return list(csv.DictReader(stream, delimiter="\t"))

    def test_unavailable_startup_is_complete_notrun(self):
        commands = [str(self.directory / "absent")]
        for name, code in (("fails", "raise SystemExit(9)"),
                           ("empty", "pass"),
                           ("hangs", "import time; time.sleep(3)")):
            commands.append(str(self.executable(self.directory / name, code)))
        for command in commands:
            with self.subTest(command=command):
                result = self.check(shlex.quote(command))
                self.assertEqual(result.returncode, 1, result.stderr)
                rows = self.manifest()
                self.assertEqual(len(rows), 11)
                self.assertEqual({row["status"] for row in rows}, {"NOTRUN"})
                self.assertEqual([row["id"] for row in rows], sorted(row["id"] for row in rows))
                path = self.directory / "results/oracle/candidate.tsv"
                self.assertEqual(path.read_bytes(), path.with_name("expected.tsv").read_bytes().replace(b"\t0\n", b"\t0.000000\n"))
                self.assertNotIn(b"\r", path.read_bytes())

    def test_individual_outcomes_remain_distinct(self):
        interpreter = self.executable(self.directory / "outcomes", r'''
            import os, signal, sys, time
            code = sys.argv[-1]
            if "import bz2" in code:
                sys.exit(1)
            if "import ctypes" in code:
                os.kill(os.getpid(), signal.SIGTERM)
            if "import decimal" in code:
                time.sleep(3)
            if "import _tkinter" in code:
                sys.exit(77)
            if "import lzma" not in code:
                print("oracle-check-ok")
        ''')
        result = self.check(str(interpreter))
        self.assertEqual(result.returncode, 1, result.stderr)
        statuses = {row["id"]: row["status"] for row in self.manifest()}
        self.assertEqual(statuses["module.bz2"], "FAIL")
        self.assertEqual(statuses["module.ctypes"], "CRASH")
        self.assertEqual(statuses["module.decimal"], "TIMEOUT")
        self.assertEqual(statuses["module._tkinter"], "SKIP")
        self.assertEqual(statuses["module.lzma"], "ERROR")
        self.assertEqual(statuses["runtime.version"], "PASS")


class ScoreboardTests(Commands):
    def setUp(self):
        super().setUp()
        self.suite = self.directory / "example"
        self.suite.mkdir()
        (self.suite / "expected.tsv").write_text("id\tstatus\tseconds\na\tNOTRUN\t0\nb\tNOTRUN\t0\nc\tNOTRUN\t0\n")

    def scoreboard(self):
        return self.invoke([SCOREBOARD, "--results", self.directory])

    def test_missing_rows_are_notrun_and_skips_do_not_pass(self):
        (self.suite / "runtime.tsv").write_text("id\tstatus\tseconds\na\tPASS\t0.1\nb\tSKIP\t0\n")
        result = self.scoreboard()
        self.assertEqual(result.returncode, 0, result.stderr)
        row = next(csv.DictReader(result.stdout.splitlines(), delimiter="\t"))
        self.assertEqual((row["total"], row["PASS"], row["SKIP"], row["NOTRUN"], row["pass_rate"]),
                         ("3", "1", "1", "1", "33.33%"))

    def test_extra_rows_and_empty_inventories_are_invalid(self):
        (self.suite / "runtime.tsv").write_text("id\tstatus\tseconds\nz\tPASS\t0\n")
        self.assertEqual(self.scoreboard().returncode, 2)
        (self.suite / "expected.tsv").write_text("id\tstatus\tseconds\n")
        self.assertEqual(self.scoreboard().returncode, 2)


if __name__ == "__main__":
    unittest.main()
