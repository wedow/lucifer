---
id: luc-at0e
status: open
deps: [luc-qidc, luc-dlik]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1q: poll reasons, one poll handler, GC trigger and stop-the-world

The poll word's bit layout: CPython's _PY_*_BIT set (GIL drop, signals, pending calls, async exc, GC scheduled, please stop, explicit merge, JIT invalidate), then preempt, deopt-self, stress tick, remote exec, with the monitoring version in the upper bits. One lu_poll_slow(tstate) handles every bit for the interpreter (and later the JIT): BRC explicit merge drains the carrier's queue, pending calls (Py_AddPendingCall sets the bit), signals run in the main task, GC runs only from the GC bit. Allocation counts set the GC bit with cp314t's gc_should_collect rule (young threshold and long-lived ratio) so gc.callbacks start/stop phases fire at the oracle's points; gc.collect() is a stop-the-world with an empty collection until M0.2. Stop-the-world: a request bumps a global stop sequence; it completes when every attached carrier acknowledged after publishing its task's frames; detached carriers count as stopped and wait at re-attach; targeted events set the task's pending bit and its bound carrier's word.
Implements: DESIGN 5.6; FOUNDATIONS 2.5, 2.7 (gc stress site at the choke point, inert until M0.2).
Files: runtime/interp/poll.c, runtime/thread/stw.c, lib/modules/gc.py (callbacks, thresholds).
Adds tests/kernel: gc_at_poll.py.

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="gc_at_poll" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); tests/capi/stw.c: a stop requested while 4 foreign threads alternate attached work and Py_BEGIN_ALLOW_THREADS completes and restarts 1,000 times without loss, 15 runs.


- From the luc-dlik review, fix these with stop-the-world:
  - merge task-owner records while the task is still linked in interp->tasks;
  - lu_task_attach fatal-errors when attaching over an attached tstate, as CPython does;
  - make the poll-word and t->pending rewrites in install()/release_slot() atomic against cross-thread reasons;
  - unbind the gilstate task last at carrier exit.
