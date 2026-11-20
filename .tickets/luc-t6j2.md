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


## Notes (2026-11-19 race lane)

Not reproduced on master 1929ea93 under today's load: 0 crashes in 140 runs with default flags (no core to read). Two mechanisms of this family were found and fixed meanwhile: a returning unit's releases left through an exit that resumed RETURN_VALUE without its value when another thread had killed the unit (luc-349f; cores of threads_typed_items and threads_weakset_churn), and a unit killed before compile_at published its direct entry kept the entry after a collection freed it (luc-4t4e, by reading; jit_reclaim_threads kills units and collects in a loop). Left open until a run shows the crash again or the acceptance repeat has a base rate to compare with.
With luc-knb1, luc-349f and luc-4t4e (e8b5a558's tree): 0 of 100 runs with default flags, 0 of 40 with -X lucifer_jit=always.
