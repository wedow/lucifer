---
id: luc-9bfa
status: open
deps: [luc-c702, luc-glgt, luc-krum]
links: []
created: 2026-11-03T02:08:47Z
type: epic
priority: 1
parent: luc-nsmh
tags: [milestone, threads]
---
# M1.8: migration on: many tasks on few carriers, parity unchanged

Turn on the M:N policy over the structure that exists from K1 (docs/DESIGN.md 5): threading.Thread tasks on a carrier pool sized from the affinity mask and cgroup quota, per-carrier runnext and local queues with stealing of runnable tasks only, Go-style handoff of a carrier's run queue after about 20 us inside a syscall bracket, a 10 ms preemption monitor, task idents for _thread.get_ident (5.7). If a corpus lane diverges because an extension keys identity on C TLS, calling into that extension becomes a lifetime pin (module-level flag), never a global fallback.

## Acceptance Criteria

make check-sched: tests/sched/many_tasks.py (10,000 threads over Conditions, socketpairs and sleeps) matches the oracle with -X lucifer_carriers=4 while the runner samples at most 16 OS threads; tests/sched/pinned_callback.py (ctypes qsort comparator blocking on a lock released by another thread) sees one get_native_id() across each callback while 1,000 other threads migrate. ftcorpus serial/parallel and tools/numpytest run --parallel give the same PASS sets as M1.7. tests/kernel and difftest pass under -X lucifer_handoff.

