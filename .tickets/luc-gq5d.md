---
id: luc-gq5d
status: open
deps: []
links: []
created: 2026-11-24T02:57:49Z
type: bug
priority: 2
---
# class call finds __new__ by the MRO, not the inherited slot: a metaclass mro() without the builtin base breaks construction

class M(type): def mro(cls): return [cls, object]; class X(int, metaclass=M): pass; X(3) works in CPython (tp_new is inherited from int through the slots, whatever mro() says) and raises 'X() takes no arguments' here, in every lane. Found by tests/kernel/jit_isinstance_fold.py, which asks only of the classes until this is fixed.

