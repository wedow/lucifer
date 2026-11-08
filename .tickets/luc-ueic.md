---
id: luc-ueic
status: open
deps: []
links: []
created: 2026-11-08T19:44:46Z
type: task
priority: 3
tags: [jit, perf, regex]
---
# sre: generated matchers stay interpreted under the size-scaled threshold

From luc-ci4f. In cli.pygments and cli.pycodestyle the generated <sre> _entry functions are 25-30% of interpreted instructions (69M and 58M per process), the generic _match another 20%: a generated matcher of a few thousand code units compiles only after 1000 + 4 per unit calls or back-edges per site. A flat threshold for them cut pygments' interpreted instructions 69M -> 42M but added 40 ms of compile and gained no wall time (pygments 2.47 -> 2.55 s, pycodestyle 4.35 -> 4.34). The lever is smaller generated code (fewer units per opcode, shared helpers for repeats and charsets), which lowers both the threshold and the compile.

