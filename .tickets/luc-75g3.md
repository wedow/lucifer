---
id: luc-75g3
status: open
deps: []
links: []
created: 2026-11-24T20:52:00Z
type: bug
priority: 3
assignee: Greg Wedow
parent: luc-orn7
---
# kernel program cprofile_stats fails: No module named '_lsprof'

tests/kernel/cprofile_stats fails on origin/master builds made in a builder's worktree with No module named '_lsprof' (seen at 85255bc6 while gating the interpreter dispatch stage, before and after that change). Find whether _lsprof is an accelerator port that the build skips in some configurations or a module that was never provided, whether the daily's kernel check runs this program (888 of 888 passed on 2026-11-24), and fix the build or the module.

