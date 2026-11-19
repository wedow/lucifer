---
id: luc-9okd
status: open
deps: []
links: [luc-knb1]
created: 2026-11-19T19:30:00Z
type: task
priority: 3
tags: [perf, interpreter]
---
# the inline-cache seqlock costs the interpreter alone up to 9% on attribute-heavy rows

Accepted with luc-knb1, whose seqlock makes every specialized LOAD_GLOBAL, LOAD_ATTR and STORE_ATTR in the interpreter load the rewrite sequence before its words and again after (in place of the version's second read). Core lease, -X lucifer_jit=off, steady instructions per iteration, fixed / master 1929ea93: pyperformance richards 1.089, deltablue 1.041, raytrace 1.018, chaos 1.008 (medians of 3); AWFY Richards 0.993, DeltaBlue 1.003, Json 1.004, Havlak 1.007. Default flags are unchanged (AWFY geomean 1.000): compiled code reads no cache. To look at: where the extra instructions go in the handlers (perf annotate), whether the sequence can stay in a register across the handler's calls, and whether the parity test can go (a writer that makes the site generic before the odd sequence and sets the opcode after the even one).
