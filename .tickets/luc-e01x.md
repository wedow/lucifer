---
id: luc-e01x
status: closed
deps: []
links: []
created: 2026-11-03T09:48:24Z
type: feature
priority: 0
parent: luc-66sy
tags: [kernel]
---
# M0.0b: _sre port and the re module

Port _sre (CPython Modules/_sre/sre.c semantics; PyPy rpython/rlib/rsre as structural reference) as kernel Python over minimal primitives, so the unmodified Lib/re works: match/search/fullmatch/sub/split/finditer, groups, flags, lookaround, backrefs, possessive/atomic (3.11+), unicode classes.

## Acceptance Criteria

tests/kernel re programs (including a seeded random differential over patterns and inputs) match the oracle; libtest test_re pass rate reported


## Notes

**2026-11-03T10:28:40Z**

_sre ported as lib/modules/_sre.py; re, enum frozen; re_* kernel programs match the oracle (corpus of re_tests, 3 seeded differentials, long inputs). test_re not runnable: unittest, os, warnings, locale, copy, pickle, unicodedata, test.support are not importable. \N{name} needs a unicodedata module.
