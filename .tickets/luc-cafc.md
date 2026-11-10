---
id: luc-cafc
status: open
deps: []
links: []
created: 2026-11-10T17:44:37Z
type: task
priority: 2
assignee: Greg Wedow
---
# check: run the threads_* kernel tests under -X lucifer_handoff

luc-un7i's race hung 11 of 20 under handoff but passed 20 of 20 in the plain and jit lanes. A handoff lane over tests/kernel/threads_*.py in make check would catch such races on every merge.

