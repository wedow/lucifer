---
id: luc-cqlp
status: closed
deps: []
links: []
created: 2026-11-19T01:58:20Z
type: bug
priority: 2
tags: [threads, crash]
---
# threads_weakset_churn SIGSEGV ~1 in 30 on master

tests/kernel/threads_weakset_churn.py dies with SIGSEGV (exit -11, no output) about 1 run in 30 on master b804b96: 10 of 300 direct runs (default JIT mode) crashed; the kernelcheck plain lane caught it once. Found while gating luc-ka5h, whose branch had 0 of 300 in the same batch (the crash depends on timing, so that is no fix). Repro: run the program 300 times and count empty outputs.


## Root cause

The same as luc-349f. Core on master 1929ea93 (1 of 60 runs): WeakSet.__contains__'s `wr = ref(item)` stored NULL; the kernel type.__call__ frame behind ref() had been run by a unit that was dead, and left at RETURN_VALUE through one of its return's releases, whose exits all had nstack 0 and NULL locals, so the interpreter returned NULL. Fixed with luc-349f (a returning root's releases take no state).

threads_weakset_churn, runs exiting 139: master 1/60, fixed 0/60.
