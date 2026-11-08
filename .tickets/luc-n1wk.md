---
id: luc-n1wk
status: in_progress
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# big-int bitwise and shifts without scratch copies

long_bitwise copies both operands into two's-complement scratch buffers and the result into a third, then copies again into the int; the shifts allocate scratch too. 94% of base64's samples (b32encode's 40-bit ints). Compute into the result directly, with a machine-word path for operands of up to two digits. Census 2026-11-08 mechanism 12.

