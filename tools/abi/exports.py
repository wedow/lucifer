#!/usr/bin/env python3
"""Print the Py*/_Py* dynamic exports of a cp314t executable as a TSV.

Usage: tools/abi/exports.py PYTHON3.14t > results/abi/cp314t-exports.tsv
Columns: name, kind (nm letter), size in bytes ("-" when nm reports none).
"""

import re
import subprocess
import sys


def exports(binary):
    out = subprocess.run(["nm", "-D", "--defined-only", "-S", binary],
                         capture_output=True, text=True, check=True).stdout
    rows = {}
    for line in out.splitlines():
        fields = line.split()
        name, kind = fields[-1], fields[-2]
        if re.match(r"_?Py", name):
            size = int(fields[1], 16) if len(fields) == 4 else None
            rows[name] = (kind, size)
    return rows


def main():
    print("name\tkind\tsize")
    for name, (kind, size) in sorted(exports(sys.argv[1]).items()):
        print(f"{name}\t{kind}\t{'-' if size is None else size}")


if __name__ == "__main__":
    main()
