#!/usr/bin/env python3
"""Fail on writable global state that has no reason to exist.

Usage: tools/abi/globals.py EXPORTS.tsv GLOBALS.tsv BINARY
Every writable symbol nm reports in BINARY (types D, d, B, b) must be one of:
- an exported oracle data symbol (EXPORTS.tsv);
- the carrier's thread-local word, lu_carrier;
- a mimalloc process global (defined under third_party/mimalloc);
- a row of GLOBALS.tsv: a name or fnmatch pattern, then the reason.
"""

import csv
import fnmatch
import subprocess
import sys

CARRIER = "lu_carrier"


def writable(binary):
    """Yield (name, source location) of every writable symbol."""
    out = subprocess.run(["nm", "-l", "--defined-only", binary],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        where = line.split("\t")[1] if "\t" in line else ""
        fields = line.split("\t")[0].split()
        if fields[-2] in "DdBb":
            yield fields[-1], where


def main():
    exports, globals_tsv, binary = sys.argv[1:4]
    with open(exports, newline="") as f:
        oracle = {r["name"] for r in csv.DictReader(f, delimiter="\t")}
    with open(globals_tsv, newline="") as f:
        rows = [r for r in csv.DictReader(f, delimiter="\t")]
    for r in rows:
        if not r["reason"].strip():
            sys.exit(f"{globals_tsv}: no reason for {r['name']}")
    patterns = [r["name"] for r in rows]
    bad = [f"{name} {where}".strip() for name, where in writable(binary)
           if name not in oracle and name != CARRIER
           and "third_party/mimalloc/" not in where
           and not any(fnmatch.fnmatchcase(name, p) for p in patterns)]
    for b in bad:
        print(f"unexplained writable global: {b}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
