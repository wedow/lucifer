---
id: luc-071a
status: open
deps: []
links: []
created: 2026-11-10T22:13:48Z
type: task
priority: 2
tags: [perf, jit]
---
# Kernel dispatchers still call slot wrappers generically

After luc-custom4 (float powers, sequence and bytes concatenation, str in str, int of a float done natively), the -X lucifer_jit_stats whynot sweep of the 15 programs and mako still shows slot wrappers called through h_call from the kernel's dispatchers, per 2 loops: pyflate int.__index__ 131k, int.__rmul__ 89k, bytes.__mul__ 86k, list.__delitem__ 36k (and is_finalizing() 136k, ord() 151k as builtins); nqueens list.__setitem__ 81k and range.__reversed__ 81k; go m_log 27k; mako dict.__getitem__ 9k, type.__getattribute__ 9k. Each costs a tuple and the wrapper's dispatch. Either the operator sites take the C path (as for float ** int, list + list), or a known slot wrapper's call compiles to its slot (wrap_binaryfunc, wrap_objobjargproc, wrap_unaryfunc) without the tuple.

