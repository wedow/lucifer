#!/usr/bin/env python3
"""Check a binary's dynamic exports against the cp314t export list.

Usage: tools/abi/audit.py EXPORTS.tsv BINARY
Every listed symbol must be exported; data symbols must have the listed size.
A strong function (nm T) counts as implemented, a weak one (W) as a stub.
"""

import csv
import sys

from exports import exports

DATA = set("BDRV")


def main():
    tsv, binary = sys.argv[1:3]
    with open(tsv, newline="") as f:
        want = {r["name"]: (r["kind"], None if r["size"] == "-" else int(r["size"]))
                for r in csv.DictReader(f, delimiter="\t")}
    have = exports(binary)
    data = sum(kind in DATA for kind, _ in want.values())
    problems = []
    present = sizes = implemented = stubs = 0
    for name, (kind, size) in sorted(want.items()):
        if name not in have:
            problems.append(f"missing {name}")
            continue
        present += 1
        got_kind, got_size = have[name]
        if kind in DATA:
            if got_kind not in DATA:
                problems.append(f"{name}: data in oracle, {got_kind} here")
            elif got_size != size:
                problems.append(f"{name}: size {got_size}, oracle {size}")
            else:
                sizes += 1
        elif got_kind == "T":
            implemented += 1
        elif got_kind == "W":
            stubs += 1
        else:
            problems.append(f"{name}: function in oracle, {got_kind} here")
    functions = len(want) - data
    print(f"exported:   {present}/{len(want)}")
    print(f"data sizes: {sizes}/{data} match")
    print(f"functions:  {implemented} implemented, {stubs} stubs, of {functions}")
    for line in problems[:40]:
        print("  " + line)
    if len(problems) > 40:
        print(f"  ... {len(problems) - 40} more")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
