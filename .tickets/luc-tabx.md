---
id: luc-tabx
status: open
deps: []
links: []
created: 2026-11-04T19:51:51Z
type: bug
priority: 3
tags: [runtime, capi]
---
# class slots for dunders a builtin defines through another slot (resolve_slotdups)

A Python subclass of list, str or tuple gets an nb_multiply thunk beside sq_repeat; CPython's update_one_slot (resolve_slotdups) leaves nb_multiply NULL, and a subclass defining __mul__ gets nb_multiply and no sq_repeat. C code that dispatches on which of the two exists (NumPy's gentype_multiply repeats a sequence that has sq_repeat and no nb_multiply) sees a different shape: LS([1]) * np.float64(2) is a TypeError in CPython, an array here. Same family: sq_concat/nb_add, sq_length/mp_length, sq_item/mp_subscript.

