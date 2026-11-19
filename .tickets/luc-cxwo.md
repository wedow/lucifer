---
id: luc-cxwo
status: closed
deps: []
links: []
created: 2026-11-19T03:17:05Z
type: task
priority: 1
tags: [jit, perf]
---
# array and bytearray items read and stored in line under typecode guards

Gap report 2026-11-18 mechanism #5, stage 1. Every a[i] on an array went through array.__getitem__/__setitem__, a _DIRECT dict probe and an h_prim_call of typed_get/typed_set (subtype check, typecode offset, critical section): about 45% of scimark. The primitives take the array typecode itself; the tier learns an immortal value at a site (value profiles beside type profiles) and lowers typed_get/typed_set on a known bytearray class with a typecode the site met alone to TYPED_LOAD/TYPED_STORE: the bytearray's lock taken in line, bounds in items, the item moved raw. Exact bytearray b[i] and b[i] = v take the same ops as typecode 'B'. The typecode is a __slots__ member: LOAD_ATTR_SLOT is in line too (SLOT_LOAD, the reference taken in place for an immortal or owned value, C for others), replacing H_slot_load. array.py's slow paths decide through the primitives under the storage's lock (they raced a concurrent resize: struct.error), and a[1.0] says 'array indices must be integers' as CPython does. Lane 'typed' (tests/kernel/jit_typed_items.py), race test threads_typed_items.py.


## Results

Core lease, steady instructions per iteration, master 7937512e = 1 (with luc-lfnr on top): pp scimark 0.144 (cycles 0.21), raytrace 0.946-0.959, float 0.964-0.969, base64 0.990, json_loads 0.987-0.992; AWFY 1.000 but Json (luc-lfnr). Firing (scimark, perf instructions:u): prim_typed_get/set, typed_offset, PyType_IsSubtype and h_prim_call (~30% of base) gone; executed helper calls 186 M -> 79 M per run. scimark's kernels give CPython's results in default, jit and deopt modes. Accepted loss: luc-91fo (pickle_pure_python +2.3%). Left: luc-ye9l (several typecodes per site), luc-62qv (memoryview).
