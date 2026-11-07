---
id: luc-w37d
status: open
deps: []
links: []
created: 2026-11-07T15:53:54Z
type: feature
priority: 1
assignee: Greg Wedow
tags: [bench]
---
# bench: broad corpus for the composite speed score

The README composite (tools/readme/stats: geomean of every *.on_over_cpython metric in the daily record) covers AWFY's 14 programs and the Flask app. Grow it to a broad corpus, as the Smalltalk VM's was, so the score reflects real Python rather than one suite. Lucifer is a drop-in python3: every benchmark is an unmodified script run identically by CPython 3.14t and Lucifer (same files, arguments and env; no runtime-specific flags or harness branches).
Candidates, in order: pyperformance (the community suite; run each benchmark's script as-is), a second and third web stack (Django ORM+templates, an ASGI app such as Starlette/FastAPI under a pure-Python server), template rendering (Jinja2, Mako), regex/json/text processing (re, json, csv, email parsing on real data), and a few real CLI programs (e.g. black/pyflakes over a fixed tree, sympy, networkx). Each lands as a daily metric named *.on_over_cpython, one entry per program, so the geomean picks it up with no table change. Programs Lucifer cannot run yet are listed as failing, not dropped; holding them out silently would inflate the score.
Acceptance: the daily records ≥50 programs across ≥4 suites; README shows the composite over all of them.


## Notes

**2026-11-07T19:18:01Z**

Landed tools/bench/corpus: pyperformance 1.14.0 (96 programs, dask left out) and a cli suite (6 programs) in the daily, plus awfy and web: 117 programs, 4 suites. First measurement 2026-11-07: 90 scored, 27 fail; composite 0.17x CPython 3.14t. Failures ticketed: luc-w4oh luc-puaa luc-vbs7 luc-xmvl luc-3hi8 luc-8r78 luc-oesi luc-6z5u luc-s6dj luc-r3kl luc-afo6 luc-yry8, notes on luc-tcnl luc-uzce.
