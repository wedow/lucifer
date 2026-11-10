---
id: luc-fppz
status: open
deps: []
links: []
created: 2026-11-10T03:18:18Z
type: task
priority: 2
assignee: Greg Wedow
tags: [conformance, glm]
---
# conformance: top-N PyPI packages import and test as on CPython

Idea from astral-experiments/serc, which validates byte-identical .pyc output across the top 10,000 PyPI packages. Lucifer's compiler is CPython's C compiler (identical by construction, check-compiler covers ~2.6k files), so byte identity isn't the question; drop-in behaviour is. Build a breadth corpus beyond tools/ftcorpus's 17 packages: the top N (start 500, grow to 5,000) PyPI packages with cp314t or pure wheels, pinned and hash-locked like corpus.lock.json. Per package on CPython 3.14t and Lucifer: install, import every public module (record success, exception type and message, dir() of each module), and where the sdist ships a quick test suite, run it. Metric: packages whose import (and tests) match CPython; README row; weekly run. Ticket mismatches batched by cause.

