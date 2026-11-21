---
id: luc-7aot
status: closed
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: test_datetime's _Fast classes never load (1803 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_datetime imports datetimetester twice: once with _datetime blocked (the _Pure classes, which run) and once with _pydatetime blocked (the _Fast classes). The second import fails on Lucifer, so all 1803 _Fast cases are missing and count as errors. Likely Lucifer's _datetime depends on _pydatetime (unverified). Make datetime importable with _pydatetime blocked.


## Notes

**2026-11-21T07:17:44Z**

The Python `_datetime` wrapper imported `_pydatetime`, so blocking the fallback
also blocked the primary backend. Install the vendored Python source under
`_datetime_impl` for `_datetime` so both backends load independently. The kernel
regression reproduced the import failure and then passed with the nearby builtin
and calendar checks in plain, jit, and jit-deopt. `test_datetime` collected the
same 1,807 `_Fast` case IDs as CPython 3.14t.
