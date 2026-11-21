---
id: luc-s3tf
status: closed
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


## Notes

**2026-11-21T07:42:42Z**

Fixed by 585436fb. A stack-pointer reading cannot bound interpreted kernel frames (they live on the datastack and never move the SP), so the room stays a model of CPython's C stack: a word of kernel frame per byte of carrier stack, since a kernel level takes about as many words as CPython's C takes bytes. Deepest level, CPython / before / after (jit off; jit, jit-deopt within 15%), 8 MiB: repr list 57970/9881/79136, repr dict 47430/12619/101066 (jit 108548), json.dumps 47429/16366/131070, == 40133/7535/60348, hash tuple (C, SP-checked) 130434/104309/104385. 64 KiB thread: repr list 151/75/616, == 104/57/470; 1 MiB: repr list 6978/1234/9890. 200,000 levels raise everywhere. Message suffixes from _recursion_where on to_repr, _json _encode/_parse_object/_parse_array and the compare functions; a walk stops at compiled code's frames, so json.dumps under jit-deopt can lack its suffix. JIT floor: min(192 KiB, 3/4 of the stack), so threads under 192 KiB run compiled code (jit lane 64 KiB repr dict 794). tests/kernel/kernel_stack_depth.py fails before, passes after in plain, jit, jit-deopt.
