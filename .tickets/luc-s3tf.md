---
id: luc-s3tf
status: open
deps: []
links: []
created: 2026-11-21T02:53:04Z
type: bug
priority: 2
tags: [recursion, fidelity]
---
# kernel recursion budget: a kernel level costs ~6x CPython's C stack per level, so deep containers fail far shallower

Review of 297f55f2. The kernel_room budget is the carrier's C stack size in words, charged co_framesize words per kernel frame. No leak or double count found: thresholds stay put after repeated RecursionErrors in every lane (probe below, 6 rounds of 5 blowouts each), and frame push/pop, generator suspend/finish, direct entry/return, deopt rebuild and lu_eval's C-stack check pair up. But the calibration does not match CPython: a level of repr/json over nested lists takes ~850 bytes of kernel frames in the interpreter, against ~145 bytes of C stack in CPython 3.14t.

Deepest nested list repr() succeeds at (8 MiB main stack, binary search):
- CPython 3.14t: 57856 (json.dumps 47360)
- lucifer jit=off and jit-deopt: 9840 (json 16256)
- lucifer default / jit=always: 24320-28206 (json 22784-32768; it depends on what got compiled)

In threads with threading.stack_size(N) (repr of nested list, CPython / lucifer, all lanes):
- 64 KiB: depth 100 ok / RecursionError
- 1 MiB: 2000 ok / RecursionError (plain, deopt)
- 16 MiB: 100000 ok / RecursionError

The message also drops CPython's suffix: lucifer 'Stack overflow (used 8183 kB)', CPython 'Stack overflow (used 8152 kB) while getting the repr of an object' / '... while encoding a JSON object'.

Related: with a thread stack under LU_JIT_STACK_RESERVE (192 KiB) plus room, lu_jit_enter and lu_jit_run now decline always, so such threads never run compiled code (a cliff by stack size).

Repro:
    def nest(d):
        x = []
        for _ in range(d): x = [x]
        return x
    for d in (10000, 20000, 40000):
        try: repr(nest(d)); print(d, 'ok')
        except RecursionError as e: print(d, e)
CPython: all ok. lucifer -X lucifer_jit=off: 10000+ RecursionError.

Fix direction: charge kernel frames what the C they stand for would take (or give the room a measured ratio of kernel words to C bytes), not word for word; pass the C 'where' text through.

