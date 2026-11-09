---
id: luc-tcnl
status: open
deps: []
links: []
created: 2026-11-04T13:20:57Z
type: feature
priority: 3
parent: luc-h4gc
---
# cmath module is missing

import cmath raises ModuleNotFoundError. Port Modules/cmathmodule.c (special-value tables for acos, acosh, asin, asinh, atan, atanh, cosh, exp, log, sinh, sqrt, tanh, plus phase, polar, rect, isclose) to lib/modules/cmath.py over the math primitives; compare a grid of finite and special values with the oracle. numpytest: TestComplexFunctions::test_against_cmath.


## Notes

**2026-11-07T18:46:10Z**

Fails the pyperformance sympy benchmark in the corpus (tools/bench/corpus): mpmath imports cmath.

**2026-11-09T00:33:44Z**

Fixed on luc-mods: cmath built from the tag's cmathmodule.c. test_cmath 32/33 (CPython 33; the errno test skips). sympy passes.
