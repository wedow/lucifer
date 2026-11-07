---
id: luc-r3kl
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# typing: issubclass() against a runtime Protocol with data members differs from CPython

pyperformance typing_runtime_protocols fails with TypeError: Protocols with non-method members don't support issubclass(). Non-method members: 'x' (typing.py __subclasscheck__), where CPython 3.14t completes. Reproduce with build/corpus/site/pyperformance/data-files/benchmarks/bm_typing_runtime_protocols/run_benchmark.py --worker -l 1 -w 0 -n 1 under PYTHONPATH=build/corpus/site. Corpus: tools/bench/corpus.

