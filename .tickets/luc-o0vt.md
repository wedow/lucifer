---
id: luc-o0vt
status: open
deps: []
links: []
created: 2026-11-09T05:45:20Z
type: bug
priority: 2
tags: [capi, slots]
---
# object's and type's attribute slots dispatch on type(self): C that calls a base's tp_setattro recurses

torch 2.15 (cp314t CPU wheel) fails at import with RecursionError in torch._C's module init. pybind11's metaclass sets tp_setattro = pybind11_meta_setattro, which ends in PyType_Type.tp_setattro(obj, name, value). Here PyType_Type's and PyBaseObject_Type's tp_setattro (and tp_getattro) are the kernel thunk lu_slot_tp_setattro, which looks __setattr__ up on type(obj): it finds the metaclass's own wrapper of pybind11_meta_setattro, which calls the thunk again, forever (lu_slot_tp_setattro -> setattr_hook -> wrap_setattr -> pybind11_meta_setattro -> ...). The same loop hits any extension whose tp_setattro or tp_getattro ends in PyBaseObject_Type.tp_setattro/tp_getattro, a common C pattern. In CPython those slots are type_setattro/PyObject_GenericSetAttr, which never dispatch on type(self).

## Design

Give object and type C slot functions that run their own kernel __setattr__/__getattribute__/__delattr__ (generic_setattr, _type_set, ...) instead of the dynamic thunk, and include them wherever ceval.c, descr.c (lu_setattr_native) and slots.c (attribute_thunk, keeps_object_hook) test for the thunk, so the fast paths keep firing. Test: tests/capi/ready.c, a static metaclass whose tp_setattro calls PyType_Type.tp_setattro and a static type whose tp_setattro calls PyBaseObject_Type.tp_setattro, each used from a Python class, against the oracle.

## Acceptance Criteria

import torch gets past torch._C's init; the two ready_run cases match CPython.

