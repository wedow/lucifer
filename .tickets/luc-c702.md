---
id: luc-c702
status: open
deps: [luc-vvqv, luc-74bf]
links: []
created: 2026-11-03T02:08:33Z
type: feature
priority: 1
parent: luc-nsmh
tags: [runtime, threads]
---
# park/unpark: every blocking primitive on lu_park or the syscall bracket

Route every blocking operation through the two entry points of docs/DESIGN.md 5.5: thread start and join, PyMutex and the parking lot, _thread.lock, RLock, Condition, queue, Event, time.sleep and all timeouts on lu_park; file I/O, waitpid, getaddrinfo, select/poll/epoll_wait, socket calls, flock and tty reads inside lu_syscall_begin/end. Lock and TSS ownership keyed on the task ident (5.4). Thread-scoped platform primitives carry the descriptor flag and pin for life. Still the degenerate one-task-per-carrier scheduler.

## Acceptance Criteria

tools/libtest run --python bin/lucifer --modules test_threading,test_thread,test_queue,test_socket,test_select,test_selectors,test_subprocess,test_time,test_io gives the same PASS set with and without -X lucifer_handoff. The make check lint finds no blocking call outside the bracket in runtime/.
- Foundations: tests/kernel/fork_threads.py (4 threads allocating and contending a Lock, 20 forks, each child starts and joins a thread and exits 0) matches the oracle; test_fork1 3/3; a running daemon thread at exit and a joined non-daemon thread give the oracle's output and exit code; ftcorpus weakref/finalizer cases pass serially and in parallel (docs/FOUNDATIONS.md 2.9, 2.13, 2.14).
