---
id: luc-ubqk
status: closed
deps: []
links: []
created: 2026-11-15T11:56:49Z
type: bug
priority: 2
assignee: Greg Wedow
---
# grp/pwd enumeration races: getgrent from several threads unlocked

runtime/prim/passwd.c prim_grp_all (and likely the pwd equivalent) calls setgrent/getgrent/endgrent from several threads with no lock; the libc enumeration state is process-global. Lib/test test_free_threading.test_grp test_racing_test_values crashes (SIGSEGV) or fails on master, JIT off too. CPython serializes these with a module lock. Fix: one lock around each enumeration (and getpwent likewise), and use the reentrant *_r variants where they exist.

