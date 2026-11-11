---
id: luc-071a
status: closed
deps: []
links: []
created: 2026-11-10T22:13:48Z
type: task
priority: 2
tags: [perf, jit]
---
# Kernel dispatchers still call slot wrappers generically

After luc-custom4 (float powers, sequence and bytes concatenation, str in str, int of a float done natively), the -X lucifer_jit_stats whynot sweep of the 15 programs and mako still shows slot wrappers called through h_call from the kernel's dispatchers, per 2 loops: pyflate int.__index__ 131k, int.__rmul__ 89k, bytes.__mul__ 86k, list.__delitem__ 36k (and is_finalizing() 136k, ord() 151k as builtins); nqueens list.__setitem__ 81k and range.__reversed__ 81k; go m_log 27k; mako dict.__getitem__ 9k, type.__getattribute__ 9k. Each costs a tuple and the wrapper's dispatch. Either the operator sites take the C path (as for float ** int, list + list), or a known slot wrapper's call compiles to its slot (wrap_binaryfunc, wrap_objobjargproc, wrap_unaryfunc) without the tuple.


## Notes

**2026-11-11T03:37:32Z**

Not a lever, dropped (not landed). Tried on luc-custom5: the common slot wrappers (binary and reflected operators, comparisons, __len__/__hash__/__bool__, __contains__, the item slots, __next__, descriptor __get__/__set__) called without their argument tuple, unbound and as method-wrappers. Instructions per loop against master, master/branch: pickle_pure_python 3691/3706, json_dumps 4507/4570, unpickle 2042/2043, pyflate 62564/62568, nqueens 12990/12996, go 14907/14900, mako 5752/5751, telco 3029/3007: noise. The generic wrapper calls left in pickle and json are too few to cost anything; the largest, property.__get__ (66k-82k a run), comes from the IOBase.closed loads of luc-l0vb.
