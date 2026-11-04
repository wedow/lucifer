---
id: luc-kvev
status: open
deps: []
links: []
created: 2026-11-04T11:33:22Z
type: task
priority: 2
assignee: Greg Wedow
---
# park/fork leftovers: pre-sleep signal check, mimalloc locks across fork

From the luc-c702 review, unproven: (1) lu_park checks the pending-signal bit only after a sleep returns, so a signal landing between lu_syscall_begin and the first wake-word read is seen only at the deadline; check before sleeping. (2) redirect_to_main_task can bounce a signal between carriers while the main task migrates. (3) mimalloc's internal locks are not in the fork registry; audit against CPython's _PyRuntimeState_ReInitThreads / mimalloc fork handling.

