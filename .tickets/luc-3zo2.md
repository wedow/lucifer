---
id: luc-3zo2
status: open
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: CD's float boxes from phis with constant inputs

Census (luc-k5az): after virtual instances, compiled code still allocates 6.65M floats per AWFY CD iteration, most at float.__truediv__, float.__sub__ and float.__add__ inlined into _is_in_voxel (cd.py:536-547), whose low_x/high_x are phis of a computed float and a module-level float constant (_inf_negative/_inf_positive), then swapped. unbox_phis keeps a phi boxed unless every input is a box, so each quotient is boxed. Unverified: treat a float (int) constant input as a box of a raw constant there.

