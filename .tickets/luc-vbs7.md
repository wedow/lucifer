---
id: luc-vbs7
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _csv is missing; chameleon fails and sphinx builds nothing

No module named '_csv'. pyperformance chameleon fails at import (csv QUOTE_STRINGS); sphinx's benchmark swallows the error (Extension error: Could not import extension sphinx.directives.patches) and finishes in 3% of CPython's time, which the corpus records as wrong output (stderr differs). Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _csv is a builtin Python port of Modules/_csv.c. test_csv 130/134 as on CPython (4 refleak skips). chameleon and sphinx pass the corpus.
