---
id: luc-z5am
status: open
deps: []
links: []
created: 2026-11-24T19:54:25Z
type: bug
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# kernel program c_recursion_limit fails on master

tests/kernel/c_recursion_limit fails on origin/master (seen at 0a564ad9 while gating the interpreter call stage, luc-2r1l: the same failure before and after that change, in every lane the builder ran). Not yet looked at: reproduce with tools/kernelcheck in jit-off and plain, find the commit that broke it by bisecting from the 2026-11-23 daily (888/888 kernel checks passed in the 2026-11-24 daily's build, so either the program is newer than that build or a landing of 2026-11-24 broke it), fix, and say why no landing gate ran it.

