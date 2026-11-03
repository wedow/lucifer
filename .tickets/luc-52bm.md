---
id: luc-52bm
status: closed
deps: [luc-o9xh]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1d: int and bool

Primitives: compact-int add/sub/mul/compare with overflow flag; digit-vector kernels ported from Objects/longobject.c (add, sub, mul incl. Karatsuba, divrem incl. x_divrem, pow incl. modular and negative-exponent inverse, shifts, bitwise on two's complement, base conversion both ways with the int_max_str_digits limit, from/to bytes, bit_length, bit_count). Kernel int/bool in Python: every int method and dunder, NotImplemented reflection, __index__, bool singletons, int(x, base) parsing with underscores. Opcodes: BINARY_OP (all NB_* incl. in-place), COMPARE_OP, UNARY_NEGATIVE, UNARY_INVERT, UNARY_NOT, TO_BOOL, IS_OP, CONTAINS_OP generic path; binary-op reflection lives in kernel Python (no C recursion). Exact-int fast paths call the compact primitives.
Implements: DESIGN 4.1 (int payload), 4.4 (scalar primitives), 6.1 (fast paths).
Files: runtime/prim/int.c, lib/kernel/int.py.
Adds tests/kernel: int_arith.py, bigint.py, bool_ops.py (str() of ints via the kernel; output printed by print).

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="int_arith bigint bool_ops" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T04:51:09Z**

Done. int/bool kernel over runtime/prim/int.c digit primitives; BINARY_OP/COMPARE_OP/unary/CONTAINS/TO_BOOL with kernel reflection (operators.py); numeric slot thunks. Deferred: int.__format__ with a spec (K1e), non-ASCII digits in int(str), bytes-like other than bytes in from_bytes (K1g), sequence concat/repeat fallbacks (K1e/K1h).
