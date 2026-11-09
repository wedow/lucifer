---
id: luc-49vz
status: in_progress
deps: []
links: []
created: 2026-11-09T09:30:06Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: a marked site whose rebuilt unit keeps its failing guard exits every iteration (abs(-v) storm)

Pure-python gap lever 7. abs(-v) over floats leaves its loop unit at UNARY_NEGATIVE's guard on every iteration and never recompiles. The policy marks the site generic after GUARD_FAILS, but a unit built after the mark keeps the same guard; that exit counts as a leave, and a unit with loops is never retired, so the storm is permanent. Find why the mark has no effect, fix it in general form, and add a jitcheck growth lane that fails on master.

