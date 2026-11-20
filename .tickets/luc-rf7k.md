---
id: luc-rf7k
status: open
deps: []
links: []
created: 2026-11-20T22:30:00Z
type: bug
priority: 2
tags: [jit, fork]
---
# jit_reclaim_fork loses the child's result about 1 run in 5 under jit-deopt

tests/kernel/jit_reclaim_fork.py run as kernelcheck runs it (the host-compiled .pyc, -I -S, 512 KiB stack) with -X lucifer_jit=always -X lucifer_stress=deopt:7 prints `1668750  3700`: the forked child's result never reaches the pipe (it exits before writing, or dies). master 766c71d7: 6 of 30 runs; the same binary on the .py source: 0 of 30. Found while gating luc-g2r0, whose build shows the same rate (5 of 30), so it predates that change. Repro: run that .pyc 30 times in the deopt lane and count lines with an empty middle field.
