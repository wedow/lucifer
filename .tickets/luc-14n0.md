---
id: luc-14n0
status: open
deps: []
links: []
created: 2026-11-08T08:03:52Z
type: task
priority: 4
tags: [jit, perf]
---
# JIT: slices with computed int bounds box the bounds

AWFY Json allocates 0.53M ints per iteration at _Parser._end_capture (json.py:353/356): self._input[self._capture_start : end + 1] boxes end + 1 for H_slice_native/H_binary_slice, which take objects. A variant taking raw int64 bounds (or None flags) for exact str, bytes, list and tuple would keep them unboxed.

