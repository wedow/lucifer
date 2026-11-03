---
id: luc-o9xh
status: closed
deps: [luc-dr76]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1c: classes, type installer, slots and attribute protocol

lib/kernel/_boot.py (functions only, no class statements) defines __build_class__ (LOAD_BUILD_CLASS; __prepare__, __init_subclass__, __set_name__, __classcell__, metaclass resolution), the Objects/typeobject.c slotdefs table ported to Python and @kernel_type(T), which installs a class body into static type T. Kernel functions look native: PyMethodDescrObject/PyCFunctionObject with a generic vectorcall trampoline and the code on a private tail; their frames are owner kernel-hidden. Static types keep tp_dict NULL; their dicts live in PyInterpreterState managed_static_type_state[] and kernel functions are reached through tstate->interp. Primitives: heap type layout (managed dict + managed weakref preheader, inline values behind shared keys), _PyType_Lookup over tp_mro, MRO computation (C3) in kernel Python. Kernel object/type: __getattribute__ (data/non-data descriptors, __getattr__ fallback), __setattr__/__delattr__, __new__/__init__ protocol, __repr__, __eq__/__ne__/__hash__ defaults, __class__ assignment checks, __slots__ (member descriptors), property, classmethod, staticmethod, super, function binding (method objects). Opcodes: LOAD_ATTR (incl. method form), STORE_ATTR, DELETE_ATTR, LOAD_SUPER_ATTR, LOAD_LOCALS, LOAD_FROM_DICT_OR_GLOBALS, SETUP_ANNOTATIONS. C slot thunks (runtime/types/slots.c) run kernel Python in a nested eval loop with pin++ (only C callers use them; the interpreter calls kernel functions directly).
Implements: DESIGN 4.4, 5.2 (slot protocols in kernel Python), 5.4 (pin on nested eval); FOUNDATIONS 2.1 (tp_dict per interpreter).
Files: lib/kernel/{_boot.py,object.py,type.py,descriptors.py}, runtime/prim/type.c, runtime/types/slots.c, tests/capi/static_tp_dict.c.
Adds tests/kernel: classes.py, descriptors.py.

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="classes descriptors" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); make check-capi passes tests/capi/static_tp_dict.c (PyLong_Type.tp_dict reads NULL after kernel init while int.__dict__ maps names; FOUNDATIONS 2.1).


## Notes

**2026-11-03T04:21:02Z**

Done: classes descriptors + 20 err_* programs match the oracle; static_tp_dict passes. Inline values behind shared keys deferred (managed dict only). For K1d/K1e/K1h/K1j: COMPARE_OP, FOR_ITER/GET_ITER, UNPACK_SEQUENCE and TO_BOOL have only exact-type fast paths with SystemError fallbacks; nb_*/sq_* slot thunks other than nb_bool/sq_length/sq_contains are absent (set_slot leaves those slots alone); kernel raises via _lucifer.error. K1k: getattr_hook's try/except AttributeError starts catching once exception tables exist. K1t: tp_version_tag assigned at readiness, cleared by lu_type_modified; subclass lists are strong lists.
