---
id: luc-a59q
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: os.sched_getscheduler and SCHED_* missing; test_posix fails to import (147 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_posix evaluates os.sched_getscheduler(0) in a skipIf at class-body time, so the whole module (147 cases) fails at import. Add os.sched_getscheduler, sched_getparam, sched_param and the SCHED_* constants.

