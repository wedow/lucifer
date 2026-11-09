---
id: luc-8r78
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _multiprocessing is missing; concurrent_imap fails

pyperformance concurrent_imap imports multiprocessing.pool, which imports _multiprocessing (ModuleNotFoundError). Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _multiprocessing and _posixshmem built from the tag's sources; os.statvfs added (multiprocessing.heap). concurrent_imap passes. test_multiprocessing_fork.test_processes 148/160 (CPython 155).
