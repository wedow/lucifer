---
id: luc-3zo2
status: closed
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: CD's float boxes from phis with constant inputs

Census (luc-k5az): after virtual instances, compiled code still allocates 6.65M floats per AWFY CD iteration, most at float.__truediv__, float.__sub__ and float.__add__ inlined into _is_in_voxel (cd.py:536-547), whose low_x/high_x are phis of a computed float and a module-level float constant (_inf_negative/_inf_positive), then swapped. unbox_phis keeps a phi boxed unless every input is a box, so each quotient is boxed. Unverified: treat a float (int) constant input as a box of a raw constant there.


## Notes

**2026-11-08T09:03:51Z**

Done on luc-esc2. unbox_phis takes a constant exact int or float input as a box of its raw value (6a3d1f7), so CD's quotients joined with _inf_negative/_inf_positive stay raw, as does any accumulator starting at a constant; a value a loop carried raw and returns is boxed at the return (6af92d0). A box an inlined initializer's parameter holds is stored raw when every exit naming it after the store also names the object; the exit makes the box once and installs it in the attribute (262abf9). CD floats per run (20 x 250): 133M -> 0.
