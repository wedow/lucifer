---
id: luc-x3m8
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: task
priority: 2
tags: [perf, floor]
---
# ft-packages JIT-off units time out: random.seed and compile cost

JIT-off floor (luc-kc4d), most ft-packages units hit their 180 s limit though test bodies sum to seconds (ujson serial: 111 of 478 done, 4.6 s of test time). cProfile of ujson serial alone, 87 s for 478 tests: random.Random.seed 20013 calls (about 42 per test, from the plugins reseeding) take 32.7 s, 1.6 ms each, in lib/modules/_random.py's Python _init_by_array; builtins.compile 88 calls take 18.1 s (pytest assertion rewriting). CPython runs the unit in 1.8 s. The run was on a loaded host (load 40-74 on 12 CPUs), which made it worse.

