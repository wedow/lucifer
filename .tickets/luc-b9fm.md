---
id: luc-b9fm
status: open
deps: []
links: []
created: 2026-11-10T03:47:41Z
type: bug
priority: 3
tags: [kernel]
---
# pow(base, exp, mod) never tries the modulus type's nb_power

lib/kernel/builtins.py pow tries base.__pow__ and exp.__rpow__ only. CPython's ternary_op also calls the third argument's nb_power when its type differs from both, so pow(10, 2, Decimal(7)) is 2 there and TypeError on Lucifer (test_decimal CCoverage.test_implicit_context). Only C types reach that branch (slot_nb_power never handles it), so it needs a primitive that calls type(mod)'s nb_power(base, exp, mod).

