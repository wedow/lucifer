---
id: luc-hlpf
status: open
deps: []
links: []
created: 2026-11-10T03:47:41Z
type: bug
priority: 3
tags: [capi]
---
# capi: static builtin types' number slots dispatch on the instance's type

An extension that calls a builtin type's slot directly, as _decimal does with PyFloat_Type.tp_as_number->nb_absolute(v) and PyLong_Type's nb_multiply/nb_floor_divide/nb_power, gets the generic slot thunk, which looks the dunder up on type(v). For a subclass that overrides __abs__ the result is the subclass's (a list in test_decimal CWhitebox.test_internal_use_of_overridden_methods), where CPython's float_abs ignores the override. A C subclass calling its base's slot from its own dunder would recurse. The static types' slots should run the base type's own method (the kernel's float.__abs__), not dispatch again.

