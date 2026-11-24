---
id: luc-qtqf
status: open
deps: []
links: []
created: 2026-11-24T03:01:59Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: descriptor and special call shapes in compiled code (property, classmethod, staticmethod, super, with)

From the first full idiom table (daily 2026-11-23; cycles per iteration, Lucifer / PyPy / ratio), the call and attribute shapes that still leave compiled code:

| idiom | Lucifer | PyPy | ratio | vs CPython |
|---|---|---|---|---|
| property read and write | 1467 | 10 | 154x | 2.7x slower |
| classmethod call | 494 | 5 | 95x | 1.1x slower |
| with statement | 498 | 5 | 105x | about equal |
| *args call | 306 | 5 | 64x | 0.5x |
| super() call | 316 | 7 | 47x | 0.6x |
| staticmethod call | 183 | 5 | 38x | 0.5x |
| global read and write | 230 | 6 | 36x | 1.1x slower |
| nonlocal read and write | 108 | 9 | 12x | 0.4x |
| getattr/hasattr with a constant name | 103 / 128 | 10 / 8 | 11x / 17x | 0.4x / 0.6x |
| reversed() iteration | 92 | 6 | 16x | 0.4x |
| __slots__ read and write | 87 | 10 | 9x | 0.6x |

Each is an ordinary language feature whose protocol is a descriptor or special-method call: a property's fget/fset is a Python function reached through the property object's __get__/__set__; classmethod and staticmethod objects are wrappers whose target is known under the class version guard; zero-argument super() resolves through __class__ and the MRO, constant under the same guard; with calls __enter__/__exit__ looked up on the type; a global store goes through the module dict.

Mechanism: under the class version guard the site already holds, resolve the descriptor at compile time and bind the call to the underlying function as an ordinary bound call (inlined or direct): property get and set to fget/fset(self[, value]); classmethod to f(cls, ...); staticmethod to f(...); super().m(...) to the function found past __class__ in the MRO; with to the type's __enter__ and __exit__, with the exception path of __exit__ kept as today's until exceptions are cheap. Global stores as a store into the dict's known entry under the dict's keys dependency.

Firing check first on each row's IR dump (what leaves, through which helper), and on three real programs for how often each shape occurs.
Acceptance: each row within 3x of PyPy; rows slower than CPython today (property, classmethod, global writes) first.

