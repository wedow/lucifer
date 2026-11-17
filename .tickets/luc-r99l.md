---
id: luc-r99l
status: open
deps: []
links: []
created: 2026-11-17T07:54:44Z
type: bug
priority: 3
---
# capi: PySet_Contains and PySet_Discard differ from CPython for non-native keys

PySet_Contains(s, []) raises TypeError 'unhashable type: list' where CPython says "cannot use 'list' as a set element (unhashable type: 'list')". PySet_Discard of a key whose __hash__/__eq__ are Python's calls __eq__ twice (a contains test, then set.discard); CPython calls it once. Seen through _testlimitedcapi.set_contains/set_discard.

