---
id: luc-rywg
status: open
deps: []
links: []
created: 2026-11-11T13:55:19Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# struct.unpack costs ~35k instructions per call

Struct('>QQ').unpack takes about 35k user instructions per call. Telco's 5,000 unpacks per loop are ~175M of its ~307M instructions. Struct stays in Python as a JIT target; find out why the compiled path costs this much (generator expression into tuple, slices, per-item dispatch) and fix the mechanism, not the benchmark.

