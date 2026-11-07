---
id: luc-8r78
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _multiprocessing is missing; concurrent_imap fails

pyperformance concurrent_imap imports multiprocessing.pool, which imports _multiprocessing (ModuleNotFoundError). Corpus: tools/bench/corpus.

