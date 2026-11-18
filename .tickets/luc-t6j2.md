---
id: luc-t6j2
status: open
deps: []
links: []
created: 2026-11-18T22:46:29Z
type: bug
priority: 1
tags: [jit, threads]
---
# jit_reclaim_threads crashes (SIGSEGV) about 1 run in 20 in the plain lane

tests/kernel/jit_reclaim_threads.py exits -11 in 1 of 20 to 40 runs with default flags, under host load: on master 538654a (1/20, 1/40) and before the direct entries landed, c503f47 (1/40); seen in kernelcheck plain during the 2026-11-18 region lane gates. Same family as luc-qexw (jit_reclaim_fork): dead units freed while other threads run compiled code.

## Acceptance Criteria

rt-style repeat (40 runs, default flags and -X lucifer_jit=always) shows no crash; the cause named in the ticket.

