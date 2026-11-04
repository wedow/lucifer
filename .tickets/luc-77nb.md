---
id: luc-77nb
status: open
deps: []
links: []
created: 2026-11-04T19:51:51Z
type: bug
priority: 4
tags: [runtime, capi]
---
# C operators consult the slot, the kernel the dunder: split hash/compare pairs across C bases

A static type with tp_bases (Hashy, Cmp), where Hashy has only tp_hash and Cmp only tp_richcompare, inherits Hashy's pair (hash, NULL) as CPython does, but the kernel's == looks up __eq__ by MRO and finds Cmp's wrapper: Multi() == 1 is True here, False in CPython. Also a type with only tp_getattr/tp_setattr shows __getattribute__/__setattr__/__delattr__ wrappers in its dict, which CPython does not add.

