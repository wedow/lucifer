---
id: luc-ik8o
status: open
deps: []
links: []
created: 2026-11-21T15:52:35Z
type: bug
priority: 2
tags: [jit, fork]
---
# jit_reclaim_fork: the forked child dies of SIGILL now and then

tests/kernel/jit_reclaim_fork.py: the child after os.fork() is killed by SIGILL (wait status 132) on some runs, so the parent prints an empty child result. Master at cc19aa67 (before luc-tax0): 7 of 120 runs under -X lucifer_jit=always -X lucifer_stress=deopt:7, 0 of 120 under -X lucifer_jit=always, 8 at once on a loaded host. Seen in kernelcheck's jit and jit-deopt lanes as FAIL jit_reclaim_fork (stdout '1668750  3700'). A ud2 reached in the child: likely code freed or reused across the fork (the test churns units in parent and child).

