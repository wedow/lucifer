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
