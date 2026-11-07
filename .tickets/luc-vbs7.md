---
id: luc-vbs7
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _csv is missing; chameleon fails and sphinx builds nothing

No module named '_csv'. pyperformance chameleon fails at import (csv QUOTE_STRINGS); sphinx's benchmark swallows the error (Extension error: Could not import extension sphinx.directives.patches) and finishes in 3% of CPython's time, which the corpus records as wrong output (stderr differs). Corpus: tools/bench/corpus.

