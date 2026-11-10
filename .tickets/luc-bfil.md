---
id: luc-bfil
status: open
deps: []
links: []
created: 2026-11-10T03:47:24Z
type: bug
priority: 2
tags: [libtest, perf]
---
# int: str(n) and int(s) are quadratic, 10-million-digit test_int case runs for hours

With _decimal built (luc-clibs), test_int PyLongModuleTests.test_pylong_roundtrip_huge runs: it converts a 10-million-digit int to and from str. CPython does it in seconds through _pylong's subquadratic paths (decimal-based int_to_decimal_string and _dec_str_to_int_inner); runtime/prim/int.c omits them, so the case outlives the module timeout and zeroes test_int (42 PASS before _decimal, 52 on CPython 3.14t). The case is excluded in results/libtest/lucifer.excluded.tsv until int<->str conversion of large ints is subquadratic (call _pylong above CPython's thresholds, as longobject.c does).


## Notes

**2026-11-10T05:18:05Z**

Also behind it: test_int PyLongModuleTests test_pylong_int_divmod_crash and test_pylong_misbehavior_error_path_{to,from}_str, which patch _pylong and expect its errors to surface; they run once _decimal exists.
