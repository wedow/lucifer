---
id: luc-1i7y
status: open
deps: []
links: []
created: 2026-11-10T02:24:39Z
type: task
priority: 2
assignee: Greg Wedow
tags: [jit, perf]
---
# JIT: unpack and local-store slow paths emit ~2.5 KB per 10-item unpack

From luc-1yki: a 10-item unpack emits about 2.5 KB, mostly per-local old-value release slow paths. pyperformance unpack_sequence's 1 MB loop body is past L2, so it now refuses to compile and runs interpreted at 89 ns vs 3.14t 55.5. Share the release slow paths (one out-of-line stub per block, as exits do) so the body fits and compiles.

