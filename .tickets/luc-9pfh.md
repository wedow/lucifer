---
id: luc-9pfh
status: open
deps: []
links: []
created: 2026-11-09T05:53:29Z
type: task
priority: 2
tags: [perf, numpy, jit]
---
# perf: Python operators on extension objects go through kernel operators and slot wrappers

cadence's Python loops index ndarrays, add into them and int() numpy scalars. In the profile py::binary_op and py::getitem (lib/kernel operators) and the wrapper descriptors they reach take 4-8% of Lucifer's time, and the JIT's whynot report for one walking_rhythm run lists 3.7M calls of wrapper_descriptor numpy.ndarray.__getitem__, 2.7M numpy.int64.__int__, 0.5M ndarray.__iadd__, 0.3M ndarray.__setitem__: each a call through the kernel's generic operator, a type lookup of the dunder and a slot wrapper with an argument tuple, where CPython calls tp_as_mapping->mp_subscript, nb_inplace_add or nb_int directly. The existing corpus is pure Python, so it never measured this path.

## Design

Give BINARY_SUBSCR/STORE_SUBSCR/BINARY_OP and int()/float()/index() a native path for a type whose slot is a C function (not a slot thunk): call the slot as CPython's abstract.c does, and let the JIT emit that call for a site whose observed type has a C slot.

