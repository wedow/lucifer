---
id: luc-yikq
status: closed
deps: []
links: []
created: 2026-11-07T23:49:21Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: direct entry binds star parameters, cells and keyword-only defaults

From luc-ibwc's whynot census (-X lucifer_jit_stats 'jit: whynot'): calls from compiled code and C to compiled functions miss the direct entry when the callee has *args/**kwargs (varargs: telco 360K, async_tree_eager 166K; the kernel's call_special, which every C-slot special call reaches, and type.__call__ from C), MAKE_CELL in its prologue (cells: async_generators 1.67M, pprint 0.9M, tomli_loads 0.3M, kernel sort/_timsort/_choose), or keyword-only parameters with defaults (kwonly: regex_dna 0.8M, tomli_loads 0.36M). Each then goes call_function's slow path (lu_frame_call, lu_eval, lu_jit_run). Bind them in the direct entry: an empty-tuple *args and a fresh **kwargs dict when called positionally, cells made after binding, keyword-only defaults from __kwdefaults__; an allocation failure after the frame is pushed leaves as an error exit (as lu_jit_run's prologue does). tools/jitcheck ccalls allows one call per iteration for call_special: drop it to 0.


## Notes

**2026-11-08T14:34:27Z**

Done in luc-p2yt: the direct entry binds keywords (from compiled code and C), keyword-only defaults, star parameters and the prologue's cells in C (lu_jit_bind) when it cannot in line; a binding that would raise declines. Census direct calls (C entering the eval loop's frame entry) per run, master -> branch: Flask 6.69M -> 8K, pprint 6.02M -> 161, tomli_loads 2.67M -> 211, raytrace 1.67M -> 253. jitcheck ccalls allows 0 per iteration now; new lane shapes.
