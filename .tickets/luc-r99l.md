---
id: luc-r99l
status: closed
deps: []
links: []
created: 2026-11-17T07:54:44Z
type: bug
priority: 3
---
# capi: PySet_Contains and PySet_Discard differ from CPython for non-native keys

PySet_Contains(s, []) raises TypeError 'unhashable type: list' where CPython says "cannot use 'list' as a set element (unhashable type: 'list')". PySet_Discard of a key whose __hash__/__eq__ are Python's calls __eq__ twice (a contains test, then set.discard); CPython calls it once. Seen through _testlimitedcapi.set_contains/set_discard.


## Notes

**2026-11-19T09:45:00Z**

The C-API fallbacks dispatched through Python membership. Contains hashed the key first and lost the set-specific TypeError. Discard checked membership before calling the Python discard method, which repeated equality and accepted mutable set keys via Python set coercion.

The fallbacks now call the existing strict kernel key operations. The C-API regression covers the exact error text, hash and equality call counts, and set subclass overrides. It failed on the base commit and matched CPython 3.14t after the fix.
