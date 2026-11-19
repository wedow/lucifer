---
id: luc-8ovx
status: open
deps: []
links: []
created: 2026-11-19T04:28:56Z
type: bug
priority: 3
---
# str[2**70] under the tier says 'string index out of range'

With the tier on, s[2**70] on an exact str raises IndexError('string index out of range'); CPython (and the interpreter) raise IndexError("cannot fit 'int' into an index-sized integer"). The kernel's str.__getitem__ (lib/kernel/str.py), which compiled code inlines for an exact str and an int, compares the key with the length without PyNumber_AsSsize_t's range check. Repro: def f(s, i): return s[i]; warm f('xyz', 1) under -X lucifer_jit=always, then f('xyz', 2**70). Pre-existing on master 7937512e.

