---
id: luc-n1wk
status: closed
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# big-int bitwise and shifts without scratch copies

long_bitwise copies both operands into two's-complement scratch buffers and the result into a third, then copies again into the int; the shifts allocate scratch too. 94% of base64's samples (b32encode's 40-bit ints). Compute into the result directly, with a machine-word path for operands of up to two digits. Census 2026-11-08 mechanism 12.


## Notes

**2026-11-08T19:52:12Z**

Landed on luc-hotc: shifts and &|^ write into the result object (two's complement on the fly, machine words for operands of up to two digits). The census's 94% was base64's module setup: randbytes(1M) through getrandbits, which OR-ed each 32-bit word into the result (quadratic). getrandbits now fills one word buffer (mt_getrandbits primitive), and k past uint64 raises OverflowError instead of looping. A whole base64 run: 192 s -> 124 s; the timed loops 14.13x -> 13.13x CPython, now spread over the Python binascii/struct code (luc-cs33 covers native base64). Tests: int_bitwise.py, random_getrandbits.py.
