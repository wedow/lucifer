---
id: luc-r5qm
status: closed
deps: [luc-2yau, luc-616t, luc-rjvn]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1j: dict, set, frozenset and comprehensions

Kernel dict over the runtime/object/dict.h keys format shared with the namespace primitive: general keys with __hash__/__eq__ callbacks outside commit regions and re-validation after (mutation during lookup), resize policy, insertion order, views (keys/items/values with set ops), dict | and |=, fromkeys, setdefault, popitem, __missing__ for subclasses, iterator-size-change errors; namespace primitives call lu_dict_notify. Kernel set/frozenset replicating Objects/setobject.c probing and resize so iteration order equals the oracle's (one probe function in runtime/object/set.h shared with the marshal reader's frozenset build). Mutators commit inside PyCriticalSection. Opcodes: BUILD_MAP, BUILD_SET, MAP_ADD, SET_ADD, SET_UPDATE, DICT_UPDATE, DICT_MERGE, CONTAINS_OP on dict/set.
Implements: DESIGN 4.1 (dict/set headers, set order), 4.4 (no bootstrap regress, atomicity); FOUNDATIONS 2.4 (dict notify path).
Files: runtime/object/set.h, runtime/prim/dict.c, lib/kernel/{dict.py,set.py}.
Adds tests/kernel: dict_ops.py, set_ops.py, comprehensions.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="dict_ops set_ops comprehensions" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T06:14:22Z**

dict, set, frozenset, views and comprehensions landed on branch luc-r5qm; gaps ticketed (mappingproxy, watcher delivery, cross-carrier commit)
