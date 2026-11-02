#!/usr/bin/env python3
"""Host-Python driver for the immutable NumPy wheel acceptance suite (stdlib only)."""

import argparse
import base64
import binascii
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
STATUSES = "PASS FAIL ERROR SKIP XFAIL XPASS TIMEOUT CRASH NOTRUN".split()
PIN = json.loads((HERE / "PIN").read_text())


def digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def matches(ident, shard):
    if "::" not in shard:
        return ident.split("::", 1)[0].startswith(shard)
    return ident == shard or ident.startswith(shard + "::") or ident.startswith(shard + "[")


def write_manifest(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="") as stream:
        stream.write("id\tstatus\tseconds\n")
        for ident, (status, seconds) in sorted(rows.items()):
            if any(c in ident for c in "\t\r\n") or status not in STATUSES:
                raise ValueError("invalid manifest row")
            stream.write(f"{ident}\t{status}\t{seconds:.6f}\n")
    temporary.replace(path)


def read_manifest(path):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t", quoting=csv.QUOTE_NONE)
        if reader.fieldnames != ["id", "status", "seconds"]:
            raise ValueError("invalid manifest header")
        rows = {}
        for row in reader:
            if row["id"] in rows or row["status"] not in STATUSES:
                raise ValueError("duplicate id or invalid status")
            rows[row["id"]] = (row["status"], float(row["seconds"]))
    if not rows:
        raise ValueError("empty expected manifest")
    return rows


def execute(command, cwd, env, log, timeout):
    """Bound the whole process group, including compilers and pytest children."""
    with log.open("wb") as output:
        try:
            child = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        except OSError as exc:
            output.write(str(exc).encode())
            return None
        try:
            return child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            return "TIMEOUT"


def probe(command, site, work, env):
    source = (
        "import json,sys,sysconfig,numpy; print(json.dumps({"
        "'version':sys.version.split()[0],'numpy':numpy.__version__,"
        "'free_threaded_build':bool(sysconfig.get_config_var('Py_GIL_DISABLED')),"
        "'gil_enabled':sys._is_gil_enabled() if hasattr(sys,'_is_gil_enabled') else None}))"
    )
    log = work / "probe.log"
    code = execute(command + ["-c", source], work, env, log, 30)
    if code != 0:
        return None
    try:
        result = json.loads(log.read_text().splitlines()[-1])
    except (ValueError, IndexError):
        return None
    if not isinstance(result, dict) or result.get("numpy") != PIN["version"]:
        return None
    return result


def verify_artifact(cache, site, abi):
    wheel = PIN["wheels"][abi]
    archive = cache / wheel["filename"]
    if digest(archive) != wheel["sha256"]:
        raise ValueError("wheel archive hash mismatch")
    binaries = {}
    checked = 0
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if entry.is_dir() or entry.filename.endswith(".dist-info/RECORD"):
                continue
            installed = site / entry.filename
            expected = hashlib.sha256(package.read(entry)).hexdigest()
            if not installed.is_file() or digest(installed) != expected:
                raise ValueError("installed wheel payload mismatch: " + entry.filename)
            checked += 1
            if ".so" in installed.name:
                binaries[entry.filename] = expected
    return {"wheel": wheel["filename"], "sha256": wheel["sha256"],
            "verified_files": checked, "native_binaries": binaries}


def junit_rows(path):
    rows, reasons = {}, {}
    for case in ET.parse(path).iter("testcase"):
        properties = {p.get("name"): p.get("value") for p in case.findall("properties/property")}
        encoded = properties.get("nodeid64")
        if not encoded:
            # Collection errors are suite diagnostics, never invented test ids.
            continue
        ident = base64.b64decode(encoded).decode()
        status = properties.get("outcome", "PASS")
        child = next((case.find(tag) for tag in ("error", "failure", "skipped")
                      if case.find(tag) is not None), None)
        if child is not None:
            status = {"error": "ERROR", "failure": "FAIL", "skipped": "SKIP"}[child.tag]
            kind = child.get("type", "")
            if child.tag == "skipped" and kind == "pytest.xfail":
                status = "XFAIL"
            if child.tag == "failure" and ("[XPASS(strict)]" in (child.text or "")):
                status = "XPASS"
            if re.search(r"Timeout (?:>|\(>)", child.text or ""):
                status = "TIMEOUT"
            reason = child.get("message", "").splitlines()[0] if child.get("message") else kind
            # Raw XML/logs remain external. Public diagnostics contain no host paths.
            reasons[ident] = re.sub(r"(?<!\w)/[^\s'\"<>]+", "<path>", reason)[:300]
        value = (status, float(case.get("time", "0")))
        if ident in rows:
            # JUnit may emit separate call and teardown failures for one item.
            old = rows[ident]
            order = {s: i for i, s in enumerate(["PASS", "SKIP", "XFAIL", "XPASS", "FAIL", "ERROR", "TIMEOUT", "CRASH", "NOTRUN"])}
            value = (max((old[0], status), key=lambda s: order[s]), old[1] + value[1])
        rows[ident] = value
    return rows, reasons


def journal_rows(path):
    """Retain completed tests if a crash prevents pytest from closing JUnit."""
    rows = {}
    durations = Counter()
    if path.exists():
        for line in path.read_text().splitlines():
            try:
                report = json.loads(line)
            except ValueError:
                continue  # A killed process can leave one final partial line.
            ident = report["id"]
            durations[ident] += report["seconds"]
            if report["outcome"] == "failed":
                status = "FAIL" if report["when"] == "call" else "ERROR"
            elif report["outcome"] == "skipped":
                status = "XFAIL" if report["wasxfail"] else "SKIP"
            elif report["when"] == "call":
                status = "XPASS" if report["wasxfail"] else "PASS"
            else:
                if ident in rows:
                    rows[ident] = (rows[ident][0], durations[ident])
                continue
            rows[ident] = (status, durations[ident])
    return rows


def prepare(args):
    args.venv = args.venv.resolve()
    subprocess.run([args.oracle, "-m", "venv", "--clear", str(args.venv)], check=True)
    python = args.venv / "bin/python"
    wheel = PIN["wheels"][args.abi]
    if digest(args.cache / wheel["filename"]) != wheel["sha256"]:
        raise ValueError("wheel archive hash mismatch")
    with tempfile.TemporaryDirectory(prefix="numpytest-install-") as temporary:
        requirement = Path(temporary) / "numpy.txt"
        requirement.write_text(f"numpy=={PIN['version']} --hash=sha256:{wheel['sha256']}\n")
        subprocess.run([str(python), "-m", "pip", "install", "--force-reinstall", "--no-index", "--only-binary=:all:",
                        "--find-links", str(args.cache.resolve()), "--require-hashes",
                        "-r", str(HERE / "requirements.txt"), "-r", str(requirement)], check=True)


def run(args):
    suite = "numpytest-parallel" if args.parallel else "numpytest"
    directory = args.results / suite
    expected_path = directory / "expected.tsv"
    command = shlex.split(args.python)
    shards = args.shard or [""]
    label = shards[0].replace("/", "_") if len(shards) == 1 else hashlib.sha256("\n".join(shards).encode()).hexdigest()[:12]
    if command and "/" in command[0]:
        command[0] = str(Path(command[0]).resolve())
    site = args.site.resolve() if args.site else None
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.runtime + "-", dir=args.work.resolve()))
    env = os.environ.copy()
    env.pop("PYTHONHOME", None)
    env.pop("PYTHON_GIL", None)
    env.pop("PYTEST_ADDOPTS", None)
    env.update({"PYTHONPATH": os.pathsep.join([str(HERE)] + ([str(site)] if site else [])),
                "PYTEST_DISABLE_PLUGIN_AUTOLOAD": "1", "PYTHONHASHSEED": "0",
                "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1",
                "NUMPYTEST_SITE": str(site) if site else "",
                "NUMPYTEST_COLLECTION": str(work / "collection.json"),
                "NUMPYTEST_STATE": str(work / "state.json"),
                "NUMPYTEST_ACTIVE": str(work / "active.txt"),
                "NUMPYTEST_REPORTS": str(work / "reports.jsonl"),
                "NUMPYTEST_SHARDS": json.dumps(shards)})
    if site:
        # f2py spawns the venv's meson/ninja executables; Lucifer can use the
        # same installation layout without rebuilding or copying the wheel.
        env["PATH"] = str(site.parents[2] / "bin") + os.pathsep + env.get("PATH", "")
    expected = read_manifest(expected_path) if expected_path.exists() else {}
    selected = {i for i in expected if any(matches(i, s) for s in shards)}
    rows = {i: ("NOTRUN", 0.0) for i in expected}
    destination = directory / (args.runtime + ".tsv")
    if args.shard and destination.exists():
        rows.update({i: v for i, v in read_manifest(destination).items() if i in expected})
        rows.update({i: ("NOTRUN", 0.0) for i in selected})
    metadata = {"numpy": PIN["version"], "abi": args.abi, "shards": shards,
                "parallel_threads": 4 if args.parallel else 1, "timeout": args.timeout,
                "requirements_sha256": digest(HERE / "requirements.txt") if (HERE / "requirements.txt").exists() else None}
    try:
        if not site:
            raise ValueError("site-packages required for an executable runtime")
        metadata["artifact"] = verify_artifact(args.cache, site, args.abi)
        state = probe(command, site, work, env)
        if state is None:
            raise ValueError("interpreter failed startup or NumPy import")
        if args.abi == "cp314t" and (not state.get("free_threaded_build") or state.get("gil_enabled") is not False):
            raise ValueError("free-threaded runtime has no confirmed disabled GIL")
        metadata["startup"] = state
        executable = shutil.which(command[0], path=env.get("PATH"))
        if executable:
            metadata["interpreter_sha256"] = digest(executable)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        metadata["unavailable"] = re.sub(r"(?<!\w)/[^\s'\"<>]+", "<path>", str(exc))
        if args.collect:
            raise ValueError("cannot establish collection: " + metadata["unavailable"]) from exc
        if not expected:
            raise ValueError("expected.tsv must be collected with the oracle first")
        write_manifest(destination, rows)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / (args.runtime + ".json")).write_text(json.dumps(metadata, indent=2) + "\n")
        print(suite, args.runtime, dict(Counter(s for s, _ in rows.values())))
        return 0
    pytest_args = ["-m", "pytest", "-p", "recorder", "-p", "pytest_timeout", "-q", "-ra",
                   "--rootdir", str(site), "--junitxml", str(work / "junit.xml"),
                   "-o", "junit_family=xunit2", "--timeout=600"]
    if args.parallel:
        pytest_args += ["-p", "pytest_run_parallel.plugin", "--parallel-threads=4"]
    if args.collect:
        pytest_args += ["--collect-only"]
    # Target one installed module when sharding, avoiding repeated full collection.
    targets = [str(site / shard) for shard in shards] if args.shard else ["--pyargs", "numpy"]
    code = execute(command + pytest_args + targets, work, env, work / "pytest.log", args.timeout)
    metadata["exitstatus"] = code
    collection_file = work / "collection.json"
    try:
        collected = set(json.loads(collection_file.read_text())) if collection_file.exists() else set()
        if not all(isinstance(ident, str) for ident in collected):
            raise ValueError("invalid node IDs")
    except (ValueError, TypeError):
        metadata["invalid_collection"] = True
        collected = set()
    if args.collect:
        if code != 0 or not collected:
            raise ValueError("oracle collection failed; inspect external pytest.log: " + str(work))
        for ident in collected:
            expected[ident] = ("NOTRUN", 0.0)
        write_manifest(expected_path, expected)
        metadata["collected"] = len(collected)
        metadata["collection_sha256"] = hashlib.sha256("\n".join(sorted(collected)).encode()).hexdigest()
        if (work / "state.json").exists():
            metadata["finish"] = json.loads((work / "state.json").read_text())
        (directory / ("collection-" + args.runtime + ".json")).write_text(json.dumps(metadata, indent=2) + "\n")
        print(suite, "expected", len(expected))
        return 0
    if not expected:
        raise ValueError("expected.tsv must be collected with the oracle first")
    # Extra collected IDs are retained and reported, never silently discarded.
    metadata["unexpected"] = sorted(collected - set(expected))
    metadata["not_collected"] = sorted(selected - collected)
    if (work / "junit.xml").exists():
        try:
            outcomes, reasons = junit_rows(work / "junit.xml")
        except (ET.ParseError, ValueError, binascii.Error, UnicodeError):
            metadata["invalid_junit"] = True
            outcomes, reasons = {}, {}
    else:
        outcomes, reasons = {}, {}
    completed = journal_rows(work / "reports.jsonl")
    completed.update(outcomes)
    rows.update(completed)
    if code == "TIMEOUT" or isinstance(code, int) and code < 0:
        active = (work / "active.txt").read_text() if (work / "active.txt").exists() else ""
        if active in selected and active not in outcomes:
            rows[active] = ("TIMEOUT" if code == "TIMEOUT" else "CRASH", 0.0)
    if (work / "state.json").exists():
        metadata["finish"] = json.loads((work / "state.json").read_text())
    metadata["reasons"] = reasons
    # Confirm tests did not modify the acceptance artifact.
    try:
        metadata["artifact_after"] = verify_artifact(args.cache, site, args.abi)
    except (OSError, ValueError, zipfile.BadZipFile):
        metadata["artifact_changed"] = True
        rows.update({i: ("ERROR", rows[i][1]) for i in selected})
    metadata["counts"] = dict(Counter(s for s, _ in rows.values()))
    write_manifest(destination, rows)
    suffix = "-" + label if args.shard else ""
    (directory / (args.runtime + suffix + ".json")).write_text(json.dumps(metadata, indent=2) + "\n")
    print(suite, args.runtime, label or "all", metadata["counts"], "exit", code, flush=True)
    finish = metadata.get("finish", {})
    if args.abi == "cp314t" and finish.get("gil_enabled") is not False:
        return 1
    failed = any(rows[i][0] in {"FAIL", "ERROR", "XPASS", "TIMEOUT", "CRASH"} for i in selected)
    return int(code != 0 or failed or bool(metadata["unexpected"]) or bool(metadata["not_collected"])
               or metadata.get("artifact_changed", False) or metadata.get("invalid_junit", False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--oracle", required=True)
    p.add_argument("--venv", type=Path, required=True)
    p.add_argument("--abi", choices=PIN["wheels"], default="cp314t")
    p.add_argument("--cache", type=Path, required=True)
    p = sub.add_parser("run")
    p.add_argument("--python", default="bin/lucifer", help="interpreter command, shell-quoted if needed")
    p.add_argument("--runtime", default="lucifer")
    p.add_argument("--site", type=Path, help="site-packages of the prepared oracle venv")
    p.add_argument("--cache", type=Path, required=True)
    p.add_argument("--abi", choices=PIN["wheels"], default="cp314t")
    p.add_argument("--work", type=Path, required=True, help="external raw XML/log directory")
    p.add_argument("--results", type=Path, default=ROOT / "results")
    p.add_argument("--collect", action="store_true", help="union oracle nodeids into expected.tsv")
    p.add_argument("--parallel", action="store_true")
    p.add_argument("--shard", action="append", help="installed numpy/ file or directory prefix; repeat to group files")
    p.add_argument("--timeout", type=int, default=540, help="whole shard timeout in seconds")
    args = parser.parse_args()
    if args.action == "prepare":
        prepare(args)
        return 0
    if not re.fullmatch(r"[a-zA-Z0-9_.-]+", args.runtime):
        parser.error("runtime must be a filename-safe label")
    return run(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print("numpytest:", exc, file=sys.stderr)
        sys.exit(2)
