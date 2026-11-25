---
id: luc-deth
status: open
deps: []
links: []
created: 2026-11-25T15:43:43Z
type: bug
priority: 3
parent: luc-c997
---
# native attribute pass runs a C getter twice when it raises

lu_descr_get_native (runtime/object/descr.c) clears the error a C getset getter or PyMember_GetOne raises and returns 0, so the caller hands the access to the kernel, which calls the getter again: a getter with side effects (a counter, I/O) runs twice per failing access, where CPython runs it once. Three call paths reach it through lu_getattr_native: LOAD_ATTR (interpreter, ceval.c op_LOAD_ATTR, and the tier's h_load_attr), PyObject_GenericGetAttr (capi/object.c), and lu_native_call's shortcuts for builtin getattr and hasattr (ceval.c).

Why it is not a two-line change: passing the raised exception through is right for LOAD_ATTR and PyObject_GenericGetAttr under the GENERIC read word, but (1) getattr(obj, name, default) and hasattr reach lu_getattr_native from lu_native_call, which treats NULL with an error as the call's result: they would raise instead of returning the default or False, so those shortcuts must catch AttributeError themselves; (2) under GENERIC_GETATTR an AttributeError from the getter must reach the type's __getattr__, which today only the kernel calls, so the native pass needs a way to hand the kernel "this AttributeError was raised, call __getattr__" without the getter running again.

Test first: a C getset (tests/capi, ready.c) that counts its calls and raises AttributeError, read through a hot site, getattr with a default, hasattr, and a subclass with __getattr__; CPython calls it once per access.

