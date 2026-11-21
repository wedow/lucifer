---
id: luc-a59q
status: closed
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: os.sched_getscheduler and SCHED_* missing; test_posix fails to import (147 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_posix evaluates os.sched_getscheduler(0) in a skipIf at class-body time, so the whole module (147 cases) fails at import. Add os.sched_getscheduler, sched_getparam, sched_param and the SCHED_* constants.


## Notes

**2026-11-21T07:25:32Z**

The scheduler query API was missing from posix, so test.test_posix stopped during class definition. Added sched_getscheduler, sched_getparam, sched_param, and the Linux SCHED_* constants exposed by CPython 3.14t. The new tests/kernel/posix_scheduler.py check failed on the missing getter before the fix and passed against CPython in plain, jit, and jit-deopt afterward. The neighboring posix_basic, posix_calls, and thread_scoped_affinity checks passed in all three lanes. test.test_posix imported and its two sched_param cases passed.
