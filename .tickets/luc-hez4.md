---
id: luc-hez4
status: in_progress
deps: [luc-v0kt, luc-pfc5, luc-15mf, luc-t0dg, luc-14n0, luc-7hlk]
links: []
created: 2026-11-08T22:55:49Z
type: epic
priority: 2
---
# JIT: per-call cost and allocation/refcount on the call path

Call protocol is 7.9% of corpus time (+2.6% JIT helper calls), allocation/refcount 7.3% (census 2026-11-08, mechanisms 3 and 5). Umbrella for luc-v0kt, luc-pfc5, luc-15mf, luc-t0dg, luc-14n0, luc-7hlk and a cheaper compiled-to-compiled frame push/pop.

