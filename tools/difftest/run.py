"""Run identical programs under an independent oracle and a candidate command."""
import argparse
from collections import Counter
import csv
import difflib
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import time

from normalize import normalize

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PROBE = '''import json, sys, sysconfig
print("DIFFTEST_PROBE=" + json.dumps({"version": list(sys.version_info[:3]),
    "implementation": sys.implementation.name, "abi": sys.abiflags,
    "free_threaded": bool(sysconfig.get_config_var("Py_GIL_DISABLED")),
    "gil": getattr(sys, "_is_gil_enabled", lambda: True)()}))
'''
FIXED_ENV = {"PYTHONHASHSEED": "0", "PYTHONIOENCODING": "utf-8:backslashreplace",
             "PYTHONUTF8": "1", "PYTHONDONTWRITEBYTECODE": "1",
             "LC_ALL": "C.UTF-8", "TZ": "UTC", "OMP_NUM_THREADS": "1",
             "OPENBLAS_NUM_THREADS": "1"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_capture(path):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("observation exceeds the 16 MiB capture limit")
    return path.read_bytes()


def environment():
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    env.update(FIXED_ENV)
    env["DIFFTEST_ROOT"] = str(ROOT)
    return env


def execute(command, cwd, timeout):
    started = time.monotonic()
    # Files bound parent memory even if a faulty interpreter floods its output.
    with (cwd / "stdout").open("wb") as out, (cwd / "stderr").open("wb") as err:
        try:
            process = subprocess.Popen(command, cwd=cwd, env=environment(),
                                       stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                       start_new_session=True)
        except OSError as error:
            return {"kind": "NOTRUN", "reason": str(error), "seconds": time.monotonic() - started}
        try:
            code = process.wait(timeout=timeout)
            kind = "CRASH" if code < 0 else "EXIT"
        except subprocess.TimeoutExpired:
            code, kind = None, "TIMEOUT"
        finally:
            # Also reap descendants of a child that exited before its own children.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    return {"kind": kind, "code": code, "seconds": time.monotonic() - started}


def probe(command, directory, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    record = execute(command + ["-c", PROBE], directory, timeout)
    try:
        output = read_capture(directory / "stdout")
        if record["kind"] != "EXIT" or record["code"] != 0 or not output.startswith(b"DIFFTEST_PROBE="):
            raise ValueError("startup probe failed")
        info = json.loads(output.split(b"=", 1)[1])
        if not isinstance(info, dict) or not {"version", "implementation", "abi", "free_threaded", "gil"} <= info.keys():
            raise ValueError("invalid startup response")
        record["info"] = info
    except (OSError, ValueError) as error:
        record["kind"] = "NOTRUN"
        record["reason"] = str(error)
    return record


def command(text):
    words = shlex.split(text)
    if not words:
        raise ValueError("interpreter command is empty")
    # Relative executable paths refer to the caller, not the private child cwd.
    if "/" in words[0]:
        # Resolving a venv symlink would bypass pyvenv.cfg and its dependencies.
        words[0] = str(Path(words[0]).absolute())
    return words


def pinned_oracle(info):
    return (info.get("version") == [3, 14, 10] and info.get("implementation") == "cpython"
            and info.get("free_threaded") and info.get("gil") is False)


def write_manifest(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(["id", "status", "seconds"])
        for name in sorted(rows):
            status, seconds = rows[name]
            writer.writerow([name, status, f"{seconds:.6f}"])
    temporary.replace(path)


def collect(args):
    cases = {}
    for path in sorted((ROOT / "tests/diff").rglob("*.py")):
        source = path.read_text(encoding="utf-8")
        name = path.relative_to(ROOT).as_posix()
        match = re.search(r"(?m)^# difftest-exit: ([0-9]+)$", source)
        cases[name] = {"sha256": digest(path.read_bytes()),
                       "state": "# difftest-state" in source,
                       "timing": "# difftest-timing" in source,
                       "exit": int(match[1]) if match else 0}
    if not cases:
        raise ValueError("empty corpus")
    dependencies = list(HERE.glob("*.py")) + [HERE / "requirements.lock"]
    dependencies += list((ROOT / "third_party/awfy/Python").rglob("*.py"))
    dependencies += [ROOT / "third_party/awfy/VERSION"]
    inventory = {"schema": 1, "cases": cases, "dependencies": {
        path.relative_to(ROOT).as_posix(): digest(path.read_bytes()) for path in sorted(dependencies)}}
    args.inventory.parent.mkdir(parents=True, exist_ok=True)
    args.inventory.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n")
    write_manifest(args.results / "expected.tsv", {name: ("NOTRUN", 0) for name in cases})
    print(f"collected {len(cases)} programs")
    return 0


def observation(record, directory, roots, spec):
    data = {"exit": record.get("code")}
    for stream in ("stdout", "stderr"):
        data[stream] = normalize(read_capture(directory / stream), roots, spec["timing"])
    if spec["state"]:
        # The state channel is structured and must never undergo repr normalization.
        state_path = directory / "state.json"
        if not state_path.exists() and record["code"] != spec["exit"]:
            data["state"] = b"<state not produced after unexpected exit>"
        else:
            data["state"] = read_capture(state_path)
            json.loads(data["state"])
    return data


def differences(left, right):
    lines = []
    for field in left:
        if left[field] == right[field]:
            continue
        a, b = left[field], right[field]
        if isinstance(a, bytes):
            a, b = a.decode("utf-8", "backslashreplace"), b.decode("utf-8", "backslashreplace")
        else:
            a, b = str(a), str(b)
        lines.extend(difflib.unified_diff(a.splitlines(keepends=True), b.splitlines(keepends=True),
                                        fromfile="oracle/" + field, tofile="target/" + field))
        # Empty final lines and missing final newlines must remain visible too.
        lines.append(f"\n{field} byte/value equality: false\n")
    return "".join(lines)


def compare_case(name, spec, oracle, target, work, loops, timeout):
    directory = (work / digest(name.encode())[:16]).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    source = ROOT / name
    if not source.is_file() or digest(source.read_bytes()) != spec["sha256"]:
        return "ERROR", 0, {"reason": "missing or changed source; recollect explicitly"}
    records, observations = {}, {}
    for arm, executable in (("oracle", oracle), ("target", target)):
        scratch = directory / arm
        scratch.mkdir(parents=True, exist_ok=True)
        state = scratch / "state.json"
        state.unlink(missing_ok=True)
        capabilities = scratch / "capabilities.json"
        capabilities.unlink(missing_ok=True)
        invocation = executable + [str(HERE / "bridge.py"), str(source), str(loops),
                                   str(state) if spec["state"] else "-"]
        record = execute(invocation, scratch, timeout)
        record["argv"] = invocation
        records[arm] = record
        if record["kind"] != "EXIT":
            if arm == "oracle":
                # An oracle crash or hang says nothing about the target.
                record["reason"] = "oracle " + record["kind"].lower()
                return "ERROR", record["seconds"], records
            return record["kind"],sum(r["seconds"] for r in records.values()), records
        if arm == "oracle" and record["code"] != spec["exit"]:
            record["reason"] = "oracle did not reach the declared exit status"
            return "ERROR", record["seconds"], records
        try:
            if capabilities.exists():
                record["after_imports"] = json.loads(read_capture(capabilities))
            elif record["code"] == spec["exit"]:
                raise ValueError("missing completion capabilities")
            observations[arm] = observation(record, scratch,
                [(scratch, "<WORK>"), (ROOT, "<ROOT>")], spec)
        except (OSError, ValueError) as error:
            record["reason"] = "missing or invalid observation: " + str(error)
            return "ERROR", sum(r["seconds"] for r in records.values()), records
    diff = differences(observations["oracle"], observations["target"])
    diff_path = directory / "mismatch.diff"
    if diff:
        diff_path.write_text(diff, encoding="utf-8")
    else:
        diff_path.unlink(missing_ok=True)
    return ("FAIL" if diff else "PASS"), sum(r["seconds"] for r in records.values()), records


def run(args):
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_.-]*", args.runtime):
        raise ValueError("runtime must be a portable filename component")
    inventory = json.loads(args.inventory.read_text())
    cases = inventory["cases"]
    if not cases or any(Path(name).is_absolute() or ".." in Path(name).parts for name in cases):
        raise ValueError("empty inventory or unsafe program id")
    rows = {name: ("NOTRUN", 0) for name in cases}
    expected = args.results / "expected.tsv"
    if expected.exists():
        with expected.open(newline="") as stream:
            names = [row["id"] for row in csv.DictReader(stream, delimiter="\t")]
        if sorted(names) != sorted(cases):
            raise ValueError("inventory and expected.tsv disagree; collect into this results directory")
    else:
        write_manifest(expected, rows)
    args.work = (args.work or ROOT / ".difftest-work" / args.runtime).resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    write_manifest(args.results / (args.runtime + ".tsv"), rows)
    oracle, target = command(args.oracle), command(args.python)
    probes = {"oracle": probe(oracle, args.work / "probe-oracle", args.startup_timeout),
              "target": probe(target, args.work / "probe-target", args.startup_timeout)}
    info = probes["oracle"].get("info", {})
    available = all(p["kind"] == "EXIT" for p in probes.values())
    if available and not (pinned_oracle(info) or args.allow_unpinned_oracle):
        available = False
        probes["oracle"]["reason"] = "requires CPython v3.14.10 free-threaded with GIL disabled"
    details = {}
    deadline = time.monotonic() + args.budget
    if available:
        changed = [name for name, sha in inventory.get("dependencies", {}).items()
                   if not (ROOT / name).is_file() or digest((ROOT / name).read_bytes()) != sha]
        if changed:
            rows = {name: ("ERROR", 0) for name in cases}
            details["dependency_errors"] = changed
            available = False
    if available:
        for name, spec in sorted(cases.items()):
            if args.select and not any(name.startswith(prefix) for prefix in args.select):
                continue
            if time.monotonic() >= deadline:
                break
            repetitions = args.awfy_loops if name.startswith("tests/diff/awfy/") else args.loops
            status, seconds, records = compare_case(name, spec, oracle, target, args.work, repetitions, args.timeout)
            if records.get("oracle", {}).get("after_imports", {}).get("gil") is not False and not args.allow_unpinned_oracle:
                if status == "PASS":
                    status = "ERROR"
                    records["reason"] = "oracle enabled its GIL after program imports"
            rows[name] = status, seconds
            details[name] = records
            # Preserve a complete denominator even if the controlling run is interrupted.
            write_manifest(args.results / (args.runtime + ".tsv"), rows)
    write_manifest(args.results / (args.runtime + ".tsv"), rows)
    metadata = {"probes": probes, "commands": {"oracle": oracle, "target": target},
                "environment": FIXED_ENV, "loops": args.loops, "awfy_loops": args.awfy_loops, "timeout": args.timeout,
                "source_inventory_sha256": digest(args.inventory.read_bytes()), "cases": details}
    (args.results / (args.runtime + ".local.json")).write_text(json.dumps(metadata, indent=2) + "\n")
    counts = Counter(status for status, _ in rows.values())
    print(args.runtime + ": " + ", ".join(f"{n} {s}" for s, n in sorted(counts.items())))
    return int(counts["PASS"] != len(cases))


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("collect", "run"))
    parser.add_argument("--python", default="bin/lucifer")
    parser.add_argument("--oracle", default=os.environ.get("DIFFTEST_ORACLE", "python3.14t"))
    parser.add_argument("--runtime", default="lucifer")
    parser.add_argument("--inventory", type=Path, default=ROOT / "tests/diff/inventory.json")
    parser.add_argument("--results", type=Path, default=ROOT / "results/difftest")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--loops", type=int, default=32)
    parser.add_argument("--awfy-loops", type=int, default=1)
    parser.add_argument("--timeout", type=positive, default=90)
    parser.add_argument("--startup-timeout", type=positive, default=5)
    parser.add_argument("--budget", type=positive, default=900)
    parser.add_argument("--select", action="append", help="ID prefix; other expected programs stay NOTRUN")
    parser.add_argument("--allow-unpinned-oracle", action="store_true", help="development/self-checks only")
    args = parser.parse_args()
    if args.loops < 1 or args.awfy_loops < 1:
        parser.error("--loops and --awfy-loops must be positive")
    try:
        return collect(args) if args.action == "collect" else run(args)
    except (OSError, ValueError, KeyError) as error:
        print("difftest: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
