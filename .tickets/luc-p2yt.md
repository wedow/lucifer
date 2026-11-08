---
id: luc-p2yt
status: closed
deps: [luc-yikq, luc-rwdf, luc-pj0a, luc-bzbq, luc-dbft]
links: [luc-uexr, luc-m3wi, luc-9ags, luc-o8i6, luc-ekol]
created: 2026-11-08T13:01:58Z
type: epic
priority: 1
tags: [jit, perf]
---
# jit: call-shape generality (keywords, star parameters, cells, class calls, forwarding, class and static methods)

Lucifer's fast call paths covered plain positional calls; Flask, Jinja, black and pyperformance are dominated by other shapes, which fell back to generic paths. Census first (luc-dbft: the call-protocol shares without the -X perf slow path, and the whynot counts per callee), then the biggest shapes in general form: the direct entry binds keywords, keyword-only defaults, star parameters and cells (luc-yikq); CALL_KW of a known function or class binds its keywords at compile time and inlines (luc-rwdf); f(*args, **kwargs) forwards without allocation in user code (luc-pj0a); closures made in inlined frames ; classmethods and staticmethods through a class or an instance bind at compile time (luc-bzbq); C-slot round trips (luc-o8i6) by measurement.


## Notes

**2026-11-08T14:37:43Z**

Landed on the branch: the direct entry binds every call shape (luc-yikq); keyword calls of known functions and classes bind at compile time and inline, ABCMeta classes lower, a class whose call is its kernel __new__'s calls __new__ (luc-rwdf); user code forwards star calls (luc-pj0a); classmethods and staticmethods bind (luc-bzbq); census redone (luc-dbft). Corpus (22 call-heavy programs, best of 3), branch/master geomean 0.941: mako 0.770, dulwich_log 0.781, telco 0.804, deltablue 0.809, logging 0.890; isort 1.021, markdown 1.015 (more inlining to compile). Flask in process 5-7% faster per route. luc-o8i6 stays open (re-scoped by measurement); leftovers luc-m3wi, luc-9ags, luc-uexr.

**2026-11-08T16:10:40Z**

Final, merged with master e57b9a4 (adaptive exit policy), on the bench host, best of 3. Corpus (22 call-heavy programs): branch/master geomean 0.946; mako 0.772, telco 0.818, deltablue 0.821, dulwich_log 0.841, logging 0.904, sqlglot_v2_parse 0.918, chaos 0.928; worst isort 1.025, sqlglot_v2_transpile 1.022, richards 1.017, markdown 1.013. AWFY best of 3 runs: geomean 0.991 (DeltaBlue 0.929, Havlak 0.958; worst List 1.011). Flask in process: 3-9% less time per route (show 1611 -> 1473 us). Web benchmark at 8 CPUs: 1998 -> 2086 req/s. Closures in inlined frames were reverted (luc-ekol reopened): they cost AWFY DeltaBlue 7% instructions.
