---
id: luc-puaa
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _pickle accelerator is missing; five pickle benchmarks fail

pyperformance's pickle, pickle_dict, pickle_list, unpickle and unpickle_list benchmarks require the C pickle (RuntimeError: Missing C accelerators for pickle); only pickle_pure_python and unpickle_pure_python run. Corpus: tools/bench/corpus.

