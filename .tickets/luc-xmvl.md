---
id: luc-xmvl
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _bz2 is missing; the three networkx benchmarks fail

networkx imports bz2 (networkx.readwrite), which raises No module named '_bz2'. Fails pyperformance networkx, networkx_connected_components, networkx_k_core. Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _bz2 built from the tag's _bz2module.c against libbz2. networkx x3 pass. test_bz2 100/102 (CPython 101; test_issue44439 is luc-ocnr).
