---
id: luc-cqlp
status: open
deps: []
links: []
created: 2026-11-19T01:58:20Z
type: bug
priority: 2
tags: [threads, crash]
---
# threads_weakset_churn SIGSEGV ~1 in 30 on master

tests/kernel/threads_weakset_churn.py dies with SIGSEGV (exit -11, no output) about 1 run in 30 on master b804b96: 10 of 300 direct runs (default JIT mode) crashed; the kernelcheck plain lane caught it once. Found while gating luc-ka5h, whose branch had 0 of 300 in the same batch (the crash depends on timing, so that is no fix). Repro: run the program 300 times and count empty outputs.

