#!/usr/bin/env python3
"""Run the whole suite as bounded shards, one run.py invocation after another.

The shard list is a function of expected.tsv alone: test files of at most
--size tests stay whole and are packed together; a larger file splits by
top-level class or function. Each shard gets its own process and --timeout, and
results merge by node ID. Arguments after `--` go to `run.py run`."""

import argparse
import collections
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def shard_groups(ids, size):
    """Lists of shard prefixes, each covering at most `size` tests (a single
    top-level class or function can exceed it)."""
    counts = collections.defaultdict(collections.Counter)
    for ident in ids:
        path, *rest = ident.split("::")
        counts[path][rest[0].split("[")[0] if rest else ""] += 1
    groups, current, weight = [], [], 0

    def add(prefix, tests):
        nonlocal current, weight
        if current and weight + tests > size:
            groups.append(current)
            current, weight = [], 0
        current.append(prefix)
        weight += tests

    for path, parts in sorted(counts.items(), key=lambda item: (-sum(item[1].values()), item[0])):
        if sum(parts.values()) <= size:
            add(path, sum(parts.values()))
        else:
            for part, tests in sorted(parts.items(), key=lambda item: (-item[1], item[0])):
                add(f"{path}::{part}" if part else path, tests)
    if current:
        groups.append(current)
    return groups


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=900, help="tests per shard")
    parser.add_argument("--results", type=Path, default=ROOT / "results")
    parser.add_argument("--parallel", action="store_true")
    parser.add_argument("--launcher", default="", help="command prefix each shard's run.py runs under")
    parser.add_argument("run_args", nargs="*", help="after --: arguments for `run.py run`")
    args = parser.parse_args()
    suite = "numpytest-parallel" if args.parallel else "numpytest"
    expected = (args.results / suite / "expected.tsv").read_text().splitlines()[1:]
    groups = shard_groups([line.split("\t")[0] for line in expected], args.size)
    status = 0
    for group in groups:
        command = [*shlex.split(args.launcher), sys.executable, str(HERE / "run.py"), "run", "--results", str(args.results),
                   *(["--parallel"] if args.parallel else []), *args.run_args]
        for prefix in group:
            command += ["--shard", prefix]
        status |= subprocess.run(command).returncode != 0
    return status


if __name__ == "__main__":
    sys.exit(main())
