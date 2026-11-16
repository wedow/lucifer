---
id: luc-svqm
status: open
deps: []
links: []
created: 2026-11-16T12:55:50Z
type: bug
priority: 1
assignee: Greg Wedow
---
# kernel oracle cache pins a stalled CPython run for timing-sensitive programs

11-16: build/tests/kernel/oracle cached CPython output for threads_shutdown ('late thread done' before 'main done') and blocked_stop_the_world ('13 of 14 blocked') recorded under load avg 40; afterwards every Lucifer run 'failed' 16/16 in that tree. Both programs' output depends on scheduling. Fix the programs to be order/timing independent (or mark nondeterministic lines), and never cache an oracle result for them.

