---
id: luc-y0wy
status: closed
deps: []
links: []
created: 2026-11-20T11:22:37Z
type: bug
priority: 3
tags: [generators]
---
# a finalizer run by a returning generator's releases sees gi_frame, CPython None

A __del__ run while a returning generator releases its locals reads gen.gi_frame: CPython 3.14t returns None there (clear_gen_frame marks the frame cleared before _PyFrame_ClearExceptCode), Lucifer a frame object, in every lane, before and after luc-kfmv. Not a memory-safety problem: the interpreter's frame object sees the cleared slots, and compiled code's frame has nothing published, so its locals read unbound. Probe: a gen(n) whose Probe local's __del__ appends gens[-1].gi_frame.f_locals when gi_frame is not None (as tests/kernel/gen_return_release_frame.py, with the generator kept in a list during the loop); CPython prints 'False 0', Lucifer 'True 0'.


## Notes

**2026-11-21T01:43:13Z**

Fixed by 0eeebe59 (same change as luc-8b3s): the ticket's probe printed 'True 0' in plain, jit and jit-deopt before and prints CPython's 'False 0' in all three after; gen_return_release_state.py checks gi_frame is None.
