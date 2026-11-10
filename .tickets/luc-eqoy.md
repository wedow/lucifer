---
id: luc-eqoy
status: open
deps: []
links: []
created: 2026-11-10T05:15:07Z
type: task
priority: 2
tags: [perf, capi]
---
# telco is 12.4x CPython with _decimal: profile the extension-call boundary

With CPython's _decimal over libmpdec (luc-clibs), pyperformance telco went from 51x to 12.4x CPython 3.14t (best of 3: 0.0105 s CPython, 0.538 s master, 0.131 s branch). The pure-Python diagnosis expected about 3.65x of tier overhead once libmpdec does the arithmetic. A short perf record of the worker puts libmpdec and _decimal at a small share and Lucifer at 85% (eval_frame 21%, dict probes, type_lookup, allocation), but the sample is small and includes startup (unverified). Profile a longer run: Decimal operators reach nb_add/nb_multiply through the kernel's generic binary_op and wrapper descriptors, and Decimal construction through type.__call__, so the extension-call path is the likely cost.


## Notes

**2026-11-10T11:58:07Z**

Re-measured on luc-custom2 (C _decimal, kernel customization): telco 10.7x CPython 3.14t wall (master 11.3x); a 20-loop worker runs 18.7G instructions against CPython's 2.2G. libmpdec plus _decimal are 2.8% of instructions, compiled code 19%, the rest the runtime. The cost is the operator path, not the extension: user-level Decimal * Decimal goes h_binary_op -> the kernel binary_op's own compiled unit -> type_lookup at run time (7% with prim_type_lookup) -> call_special of the C slot's wrapper_descriptor (h_call, lu_jit_bind, lu_jit_call about 6%), because binary_op does not inline where the special method is a C slot wrapper (two calls it cannot inline). Also each decimal operation's current_context reaches PyContextVar_Get -> PyObject_CallMethod into Python. Levers: (1) when both operand types are known and the method the kernel dispatcher folds to is a C type's nb_* slot wrapper, compile the slot call directly (binary_op1's subtype order), which serves every C extension's numbers (numpy too); (2) PyContextVar_Get without a Python call.
