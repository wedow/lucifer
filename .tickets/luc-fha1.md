---
id: luc-fha1
status: closed
deps: [luc-2yau, luc-rjvn]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1g: bytes, bytearray and basic memoryview

Kernel bytes and bytearray in Python over the storage primitives: every method (incl. hex/fromhex, decode utf-8/latin-1/ascii, translate, split family, %-formatting for bytes), repr, hashing, slicing, in-place bytearray mutation with PyCriticalSection on commit; memoryview over the buffer protocol for bytes/bytearray (tobytes, tolist, cast 'B', slicing, release). Py_buffer exports through bf_getbuffer for C callers.
Implements: DESIGN 4.1 (bytes, bytearray, memoryview payloads), 4.4.
Files: lib/kernel/{bytes.py,memoryview.py}.
Adds tests/kernel: bytes_methods.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="bytes_methods" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T05:55:42Z**

bytes, bytearray, memoryview landed on branch luc-fha1
