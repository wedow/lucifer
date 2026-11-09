---
id: luc-3hi8
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _sqlite3 is missing; coverage and sqlite_synth fail

No module named '_sqlite3': pyperformance sqlite_synth, and coverage (its data file is SQLite). Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _sqlite3 built from the tag's Modules/_sqlite against libsqlite3. coverage and sqlite_synth pass. test_sqlite3 507/514 (CPython 510).
