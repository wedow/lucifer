---
id: luc-vzka
status: in_progress
deps: []
links: []
created: 2026-11-07T10:36:17Z
type: task
priority: 1
tags: [jit, perf]
---
# JIT: inline container and allocation fast paths, three-instruction poll

Roadmap item 2 (docs: jit-roadmap section 4). LIST_ITEM and tuple items read in line (bounds, load, owner incref; try-incref only on the slow path). LIST_STORE takes ob_mutex in line as INLINE_STORE does. Int and float boxes, small tuples, lists and instances allocate in line. [x] * n and BUILD_LIST lower to allocate-and-fill instead of binary_op -> list.__mul__ -> int.__rmul__. The poll shrinks to 2-3 instructions by folding the QSBR and scheduler checks into the poll word.

## Acceptance Criteria

Per-op instruction counts for list read, list store and box drop under a core lease, with cycles agreeing. AWFY Storage and Havlak faster. threads_container_protocol and the other threads_* tests stay green; make check, check-jit, abi-audit, check-harness and check-compiler pass.

