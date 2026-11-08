---
id: luc-o8i6
status: open
deps: []
links: [luc-uexr, luc-m3wi, luc-p2yt, luc-9ags]
created: 2026-11-07T23:49:21Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: special methods of C-slot types reach kernel Python through two C calls

luc-ibwc census, tomli_loads: 42M calls per run of wrapper descriptors through call_special (str.__getitem__ 19.8M, frozenset.__contains__ 17.8M, dict/set.__contains__ 3.6M; pprint 24M). Each goes compiled kernel code -> H_call_special -> h_call -> lu_call -> lu_wrapper_descr_call (a tuple of the rest arguments) -> wrap_* -> the C slot -> lu_call of a kernel function (C entries named contains and getitem, ~21M and ~20M) -> compiled kernel code. luc-ibwc removed one tuple and the eval-loop entry; the rest remains. Either install the kernel function itself where the type's dict has the slot wrapper of a slot that only forwards to kernel Python, or let the tier lower call_special on a guarded constant wrapper descriptor to the slot (or its kernel function). Measure tomli_loads and pprint.


## Notes

**2026-11-08T14:34:27Z**

luc-p2yt's per-callee census: the wrapper-descriptor calls are kernel wrappers (kernel_wrapperbase) that call_special reaches with a descriptor only known at run time: h_call, lu_kernel_target, lu_native_call's chain, then the kernel function's direct entry. tomli_loads' 39.5M str.__getitem__ come from _sre's interpreted matcher (the regex lane's), pprint's 46M are wrapper_descriptor.__get__ (14.4M), frozenset.__contains__, type.__getattribute__ and int.__repr__, raytrace's 11M float operators from binary_op. The dispatch costs about 1-2% of those programs; the lever is knowing the operand's type where binary_op and getattr inline (operand speculation), not the call path.
