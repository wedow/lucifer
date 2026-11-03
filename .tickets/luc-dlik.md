---
id: luc-dlik
status: open
deps: [luc-rjvn]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1p: task binding, foreign threads and refcount owners

The tstate API on the task/carrier split: PyThreadState_New/Clear/Delete/Swap/Get/GetUnchecked, PyGILState_Ensure/Release/GetThisThreadState (a foreign thread gets a lifetime-pinned task bound to a carrier created for it), PyEval_SaveThread/RestoreThread and Py_BEGIN/END_ALLOW_THREADS as detach/attach (the bracket's core); bind rewrites the carrier-owned tstate fields (state, native_thread_id, gilstate_counter, _status bits, eval_breaker mirror) and re-raises the poll word if task events are pending; the degenerate scheduler binds one task per carrier for life. Biased-refcount owners register when a tstate is bound (cp314t bind_tstate) and the stopgap pthread_create wrapper in runtime/object/brc.c is deleted; tests/capi/refcount.c threads attach through PyGILState_Ensure. Identity-keyed state (lock owners, Py_tss_t) uses the task ident.
Implements: DESIGN 5.3, 5.4 (lifetime pins), 5.7 (degenerate column); FOUNDATIONS 2.1 (carrier entries per interpreter).
Files: runtime/thread/{task.c,sched.c}, runtime/capi/pystate.c, runtime/object/brc.c (wrapper removed), tests/capi/{tstate_bind.c,refcount.c}.
Adds tests/kernel: none.

Builder: opus.

## Acceptance Criteria

make check-capi passes, including check_reused_thread_id in tests/capi/refcount.c with no pthread_create wrapper in the binary (nm shows none) and tests/capi/tstate_bind.c (PyGILState_Ensure from 8 foreign threads, one stable PyThreadState_Get across each native extent, Save/Restore round trips, PyThreadState_Swap).

