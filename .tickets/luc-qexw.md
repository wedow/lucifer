---
id: luc-qexw
status: open
deps: []
links: []
created: 2026-11-18T19:26:51Z
type: bug
priority: 2
tags: [jit]
---
# jit_reclaim_fork: the forked child segfaults in 10-25% of jit-deopt runs on master

On master c503f47, tests/kernel/jit_reclaim_fork.py under -X lucifer_jit=always -X lucifer_stress=deopt:7 loses the child's output in 3-7 of 30 runs (child SIGSEGV; the parent and the oracle print three numbers). Core stacks: in JIT code entered from lu_jit_enter from module code, or in prof_exit via lu_jit_note_exit from lu_jit_deopt with a garbage d->prof. Both point at the child's code or exit tables changing under it after fork, which the test is about (parent and child compile, kill and collect units that reuse freed ranges). kernelcheck's single run passes most of the time, so the gate sees it rarely. Found by the region lane (luc-tgqa) while gating.

