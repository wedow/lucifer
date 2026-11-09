---
id: luc-14n0
status: closed
deps: []
links: []
created: 2026-11-08T08:03:52Z
type: task
priority: 4
tags: [jit, perf]
---
# JIT: slices with computed int bounds box the bounds

AWFY Json allocates 0.53M ints per iteration at _Parser._end_capture (json.py:353/356): self._input[self._capture_start : end + 1] boxes end + 1 for H_slice_native/H_binary_slice, which take objects. A variant taking raw int64 bounds (or None flags) for exact str, bytes, list and tuple would keep them unboxed.


## Notes

**2026-11-08T23:57:07Z**

Done in luc-hez4: BINARY_SLICE with a computed bound passes raw int64 bounds (None as flags) to H_binary_slice_raw/H_slice_native_raw; containers the native slice does not take box them in C. tests/kernel/jit_slices.py and the jitcheck slices lane (master: 24 ints per iteration, branch 0).
