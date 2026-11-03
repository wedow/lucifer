---
id: luc-d5u1
status: open
deps: [luc-74bf]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [jit, perf]
---
# P0: block-SSA optimizing tier; AWFY 14/14 pass

runtime/jit ported from the donor block-SSA backend: SSA from bytecode and inline caches, kernel inlining, guards, unboxing, deopt to the interpreter, background compile, perf map (docs/DESIGN.md 6.2, 6.3).

## Acceptance Criteria

tools/bench/awfy --python bin/lucifer with the SSA tier on: 14/14 PASS; difftest stays 164/164.

