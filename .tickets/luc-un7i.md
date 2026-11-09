---
id: luc-un7i
status: open
deps: []
links: []
created: 2026-11-09T00:16:09Z
type: bug
priority: 2
tags: [jit, threads]
---
# threads_set_race hangs now and then under -X lucifer_jit=always

tests/kernel/threads_set_race.py times out in the kernelcheck jit lane on master too: 2 of 8 runs of master 16a62be (found by luc-hez4's make check, where it failed once; the branch passed 8 of 8 reruns). A hang, not wrong output: the oracle's four lines never print. Find which set operation a thread waits in.

