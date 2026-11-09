---
id: luc-oesi
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _elementtree is missing; xml_etree times the pure-Python tree

Without _elementtree, pyperformance xml_etree benchmarks xml_etree_pure_python_* instead of xml_etree_*, and runs about 80x CPython's time; the corpus records it as wrong (different benchmarks) or timeout. Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _elementtree and pyexpat built from the tag's sources (pyexpat against the system Expat, replacing lib/modules/pyexpat.py). xml_etree passes. test_xml_etree_c 243/251 (CPython 247), test_pyexpat 82/87 (86; utf-7 is luc-mfrx).
