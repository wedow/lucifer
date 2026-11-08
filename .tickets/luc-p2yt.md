---
id: luc-p2yt
status: open
deps: [luc-yikq, luc-rwdf, luc-pj0a, luc-o8i6, luc-ekol, luc-bzbq, luc-dbft]
links: []
created: 2026-11-08T13:01:58Z
type: epic
priority: 1
tags: [jit, perf]
---
# jit: call-shape generality (keywords, star parameters, cells, class calls, forwarding, class and static methods)

Lucifer's fast call paths covered plain positional calls; Flask, Jinja, black and pyperformance are dominated by other shapes, which fell back to generic paths. Census first (luc-dbft: the call-protocol shares without the -X perf slow path, and the whynot counts per callee), then the biggest shapes in general form: the direct entry binds keywords, keyword-only defaults, star parameters and cells (luc-yikq); CALL_KW of a known function or class binds its keywords at compile time and inlines (luc-rwdf); f(*args, **kwargs) forwards without allocation in user code (luc-pj0a); closures made in inlined frames (luc-ekol); classmethods and staticmethods through a class or an instance bind at compile time (luc-bzbq); C-slot round trips (luc-o8i6) by measurement.

