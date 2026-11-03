---
id: luc-d47e
status: closed
deps: [luc-52bm, luc-2yau]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1f: float and complex

runtime/prim/dtoa.c adapted from Python/dtoa.c (shortest repr, correctly rounded parse; recorded in third_party/cpython/CHANGES.md), IEEE primitives (fmod, frexp/ldexp, floor/ceil, round-half-even, is_integer, as_integer_ratio, hex/fromhex), _Py_HashDouble. Kernel float and complex in Python: arithmetic with int mixing and true division of ints (correctly rounded), divmod/floor-div sign rules, pow edge cases, comparisons with big ints exactly, float.__format__/__repr__/__round__, complex repr and arithmetic, float(str) and complex(str) parsing.
Implements: DESIGN 4.1 (float, complex payloads, hashes), 4.4 (scalars).
Files: runtime/prim/{float.c,dtoa.c}, lib/kernel/{float.py,complex.py}.
Adds tests/kernel: float_ops.py, complex_ops.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="float_ops complex_ops" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T05:45:30Z**

K1f in progress: gates running

**2026-11-03T05:48:37Z**

K1f done: float and complex, dtoa/pystrtod adapted, float_ops complex_ops float_random match the oracle
