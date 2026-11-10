---
id: luc-7aot
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: test_datetime's _Fast classes never load (1803 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_datetime imports datetimetester twice: once with _datetime blocked (the _Pure classes, which run) and once with _pydatetime blocked (the _Fast classes). The second import fails on Lucifer, so all 1803 _Fast cases are missing and count as errors. Likely Lucifer's _datetime depends on _pydatetime (unverified). Make datetime importable with _pydatetime blocked.

