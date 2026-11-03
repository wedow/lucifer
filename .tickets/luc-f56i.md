---
id: luc-f56i
status: open
deps: []
links: []
created: 2026-11-03T02:02:34Z
type: task
priority: 1
assignee: Greg Wedow
tags: [runtime, review, concurrency]
---
# Review biased refcounting and merge queue (runtime/object/brc.c, refcount.c) for races

## Acceptance Criteria

Opus review with a proven failing case per defect (a stress test in tests/capi), fixes merged; or no defects with the interleavings argued


## Notes

**2026-11-03T02:14:36Z**

Review against CPython v3.14 (Objects/object.c, Python/brc.c). _Py_DecRefShared, _Py_MergeZeroLocalRefcount, _Py_ExplicitMergeRefcount, queue push and thread removal match CPython line for line.

Defect (fixed, d894e8f): a thread registered only at its first object creation. glibc reuses a joined thread's TCB, so an unregistered new thread counted a dead owner's objects locally while another thread merged them as abandoned. Lost counts, early frees. Test: check_reused_thread_id in tests/capi/refcount.c, failed in round 0 on 5 of 5 runs. Fix: every thread registers in a pthread_create wrapper before its start routine runs, as CPython registers at bind_tstate. 200 of 200 runs pass.

Interleavings argued safe:
- Queue vs owner exit: push and removal both hold the bucket lock, and the owner frees its entry only after removal. A push that lands before removal is drained by the removal loop; one that lands after finds no entry and merges directly. No use-after-free of the entry.
- Direct merge vs a registering heir with the same TCB: the merge runs under the bucket lock, and registration takes the same lock before the heir's first refcount operation. Either the heir sees ob_tid 0 or the object lands in its queue.
- Object freed while queued: while QUEUED, local + shared counts the queue's reference, so the sum is at least 1. Non-owner decrements keep flag bits QUEUED (only reaching MERGED frees). MergeZeroLocal therefore sets MERGED with count at least 1, and only the queue merge (extra -1) can reach 0.
- The ob_tid == 0 branch of queue_object: unreachable here. A successful 0->QUEUED CAS means every reference is counted in ob_ref_local, so the owner cannot be inside MergeZeroLocal. It is kept for CPython parity and is correct if reached: the owner's merge counts the queued reference and the Py_DECREF drops it.
- Re-queue/ABA: flags never return to 0 once QUEUED or MERGED, so an object is queued at most once. The queue is a mutex-protected array, so there is no lock-free ABA.
- Orderings: CAS loops are seq_cst as in CPython. The exited-owner merge reads ob_ref_local plainly, which happens-after the owner's last write through the bucket mutex.
- Churn probe (8 threads x 40 generations, 6.4M objects, references scattered through shared slots, owners exiting mid-flight): created == freed on 10 of 10 runs.

Out of scope, not fixed:
- Harness hazard: preloaded test constructors run before the executable's constructors, which include mimalloc's _mi_auto_process_init. A capi test whose first allocation happens on worker threads races mimalloc's main-heap init: several threads share _mi_heap_main and blocks are corrupted (11 of 15 runs; 0 of 15 once main allocates first). Production does not hit it, because extensions are dlopened after main starts. If needed, the fix is to run _mi_auto_process_init from .preinit_array.
- Objects queued to the main thread are never merged until thread states and the eval breaker exist (documented interim leak).
- Refcount operations from TLS destructors that run after remove_thread are unsound. CPython also forbids them without a thread state.
- No fork handling (CPython's _Py_brc_after_fork) until os.fork exists.
