"""Byte-preserving normalization of explicitly nondeterministic fields."""
import re


ADDRESS = re.compile(rb"(?<= at )0x[0-9a-fA-F]+(?=>)")
TIMING = re.compile(rb"(?m)^DIFFTEST_TIME=[0-9]+(?:\.[0-9]+)?(?:ns|us|ms|s)$")


def normalize(data, roots=(), timing=False):
    for root, replacement in sorted(roots, key=lambda pair: len(str(pair[0])), reverse=True):
        data = data.replace(str(root).encode(), replacement.encode())
    data = ADDRESS.sub(b"<ADDRESS>", data)
    if timing:
        data = TIMING.sub(b"DIFFTEST_TIME=<TIME>", data)
    return data
