---
id: luc-pl2m
status: open
deps: []
links: []
created: 2026-11-25T21:31:30Z
type: feature
priority: 2
parent: luc-orn7
---
# frames count their references to function and code on a shared word: every call and return of a function hot on several threads writes one cache line

Every call into compiled or interpreted code counts the frame's references to its function and code, and every return gives them back. When the function is hot on several threads, these are writes to one shared word per object, one cache line each, on every call and return.

Where the traffic is (after luc-o80b stage 4 and its cross-thread follow-up):
- **Entry.** The interpreter's push paths, `lu_frame_push`, and the direct-entry stub (`runtime/jit/x86_64/emit.c`, `direct_entry`) INCREF the function and the code. On a thread that does not own them, that is a `lock add` on `ob_ref_shared`.
- **Exit.** The unit's frame end (`frame_end`: a `lock sub` on the shared count for another thread's deferred function or code) or `lu_frame_end` in C (`_Py_DecRefShared`'s cmpxchg loop).
- Code objects and top-level functions are deferred, so these counts can never free them; they only cost the shared line.

CPython 3.14t avoids this with deferred stack references. Frames hold uncounted references to deferred objects, and the collector finds those references by scanning the frames of every thread. Here frames' references are counted, and the collector's `update_refs` treats them as references from outside the heap.

What a fix needs:
- **Uncounted references in frames for deferred function and code, on every path that makes or ends a frame:** the interpreter's push paths, the stub, `lu_frame_end` and the unit's frame end, frame objects on retire (the frame's copy takes counted references, or the frame object keeps the uncounted ones alive some other way), and generators' embedded frames.
- **Thread stacks' frames as collector roots for them.** First establish whether the collector already walks frames for another reason, and whether a stopped thread's compiled frames (`LU_FRAME_JIT`, inlined frames) are reachable that way.
- **A single-thread path that is unchanged or cheaper:** two INCREFs and two decrements fewer per call. By the luc-ne54 account the stub's INCREFs of code and function are 18 instructions; the return's checks and decrements in `frame_end` are about 16 more. Together that is about a fifth of the 166 instructions per direct call that `tools/bench/ladder` measures (`op.call_positional`, after stage 4).

Measured motivation (luc-o80b follow-up): with frames ending in C, AWFY Richards loaded on the main thread and run on a worker spent about 3% more instructions than with the locked fast path. Those numbers count instructions only; the contention on the shared line, which grows with the number of threads, is not measured yet.

This is the first measured item of the vertical-scaling goal (use hundreds of cores; nothing may serialize all workers) that sits in the call path.

## Acceptance Criteria

- A count of shared-word writes per call on a worker thread calling a main-thread function reads zero, where it reads four (two INCREFs, two decrements) today.
- The web rows and a threads scaling probe (N threads calling one function, N from 1 to the core count) are measured before and after.
- No single-thread row is worse by more than 0.3% in either JIT mode.

