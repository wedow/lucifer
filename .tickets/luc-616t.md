---
id: luc-616t
status: closed
deps: [luc-52bm, luc-rjvn]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1h: tuple, list, range, slice, iteration and sort

Ref-array storage primitives with refcount-correct bulk moves; kernel tuple, list, range, slice and their iterators in Python; list.sort as a port of PyPy rpython/rlib/listsort.py (timsort, key once, stability, mutation detection); list mutators bracketed by PyCriticalSection; replaced ob_item arrays freed by QSBR grace (a stub that frees at the next poll is enough while one carrier runs). Opcodes: BUILD_TUPLE, BUILD_LIST, LIST_EXTEND, LIST_APPEND, CALL_INTRINSIC_1 (LIST_TO_TUPLE, UNARY_POSITIVE), BINARY_OP NB_SUBSCR, STORE_SUBSCR, DELETE_SUBSCR, BUILD_SLICE, BINARY_SLICE, STORE_SLICE, UNPACK_SEQUENCE, UNPACK_EX, GET_ITER, FOR_ITER, END_FOR, POP_ITER, GET_LEN.
Implements: DESIGN 4.1 (tuple, list payloads), 4.2 (QSBR storage), 4.4 (atomicity).
Files: runtime/prim/storage.c (ref arrays), lib/kernel/{tuple.py,list.py,range.py,slice.py,iter.py,_listsort.py}.
Adds tests/kernel: list_ops.py, tuple_ops.py, range_slice.py, sort.py, unpack.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="list_ops tuple_ops range_slice sort unpack" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T05:24:35Z**

check-kernel list_ops tuple_ops range_slice sort unpack match the oracle (plain lane); full check-kernel 176/176, check, abi-audit, check-compiler green. Sort follows CPython 3.14 powersort (not PyPy's older merge order) so comparison counts match.
