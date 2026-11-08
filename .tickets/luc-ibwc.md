---
id: luc-ibwc
status: closed
deps: []
links: []
created: 2026-11-07T22:27:52Z
type: bug
priority: 1
assignee: Greg Wedow
tags: [jit, perf]
---
# jit: compiled-to-compiled calls never happen on the corpus

Corpus census 2026-11-07 (lucifer-notes/corpus-census-2026-11-07.md): every pyperformance and cli program's stats line says 'compiled calls 0'. Calls between compiled functions still go through h_call, lu_frame_push and lu_eval, the call protocol is 10% of corpus time, and the Py* shims on the call path are another 8%. luc-cmel's direct entry fires on AWFY but not here. Find which condition blocks it (callee not compiled at caller compile time and never relinked, call shapes such as kwargs, defaults, bound methods, closures, or methods through generic attribute loads), and make compiled code reach a compiled callee's direct entry in those cases, including linking after the callee compiles. Proof: compiled calls per run on the 30 census programs, and the pyperformance and cli geomeans.

