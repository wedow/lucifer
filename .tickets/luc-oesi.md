---
id: luc-oesi
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _elementtree is missing; xml_etree times the pure-Python tree

Without _elementtree, pyperformance xml_etree benchmarks xml_etree_pure_python_* instead of xml_etree_*, and runs about 80x CPython's time; the corpus records it as wrong (different benchmarks) or timeout. Corpus: tools/bench/corpus.

