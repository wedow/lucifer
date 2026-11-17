---
id: luc-lp19
status: open
deps: []
links: []
created: 2026-11-17T23:51:30Z
type: bug
priority: 1
---
# jit: html5lib segfaults after 10 warm-up values

pyperformance html5lib, run as the corpus's pyperformance-warm suite runs it (run_benchmark.py --worker -l 1 -w 10 -n 5 at the reference loops), dies with SIGSEGV on master e4cebea. The daily's one-warm-up shape passes, so the steady-state compiled code is the suspect. Reproduce: tools/bench/corpus time with the pyperformance-warm.html5lib program.

