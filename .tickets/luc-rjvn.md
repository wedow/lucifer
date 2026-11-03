---
id: luc-rjvn
status: closed
deps: [luc-1dl8]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1i: lu_park/lu_unpark, parking lot, PyMutex, critical sections, syscall bracket and its lint

lu_park(wait, deadline) / lu_unpark(id, result) over futexes: wait records with owner task, never-reused wait id, result slot and deadline; stale and duplicate ids ignored; an early result retained. In the degenerate scheduler every park sleeps on the carrier's futex (the unpinned return-to-carrier path lands in K1r). Python/{lock,parking_lot,critical_section}.c adapted on top (PyMutex byte in ob_mutex, PyCriticalSection[2] Begin/End/BeginMutex, _PyMutex_Lock slow path), recorded in third_party/cpython/CHANGES.md; their locks join the lock registry. lu_syscall_begin()/lu_syscall_end() = detach/attach; every blocking call in runtime/prim/os.c goes through a bracketed wrapper. make check-blocking (tools/blockcheck): objdump/nm over build/liblucifer.a members rejects any call to a blocking libc symbol (read, write, poll, futex syscall, nanosleep, waitpid, connect, accept, recv*, send*, select, epoll_wait, flock, getaddrinfo, ...) outside the wrapper file; make check aggregates check-blocking, check-globals, check-capi and check-kernel.
Implements: DESIGN 5.5 (K1 minimum), 5.6 (locks); FOUNDATIONS 2.9 (lock registry users).
Files: runtime/thread/{park.c,lock.c,parking_lot.c,critical_section.c}, runtime/prim/os.c (wrappers), tools/blockcheck, tests/capi/{park.c,mutex.c}, Makefile (check, check-blocking).
Adds tests/kernel: none.

Builder: opus.

## Acceptance Criteria

make check-capi passes tests/capi/park.c (unpark from another thread, stale and duplicate ids ignored, early result kept, deadline expiry) and tests/capi/mutex.c (8 threads contending a PyMutex and a PyCriticalSection2 on two objects, counts exact over 15 runs); make check-blocking fails on a planted unbracketed read() and passes on master.


## Notes

**2026-11-03T03:37:04Z**

check-capi passes park.c and mutex.c (15 runs); check-blocking fails on a planted read() in task.c and in os.c outside LU_SYSCALL, passes on the branch.
