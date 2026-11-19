---
id: luc-3b0b
status: closed
deps: []
links: []
created: 2026-11-19T13:37:14Z
type: bug
priority: 3
assignee: Greg Wedow
---
# JIT cached_site reads the attribute cache as several 16-bit words with only a before/after check

runtime/jit/eval_attr.c cached_site: two full respecializations inside the read window that end on the same opcode and version could hand the compiler a mixed index or kept class value, which a5b45591 (luc-20q4) would then embed. Theoretical (needs two rewrites under the specialize lock within a few loads); not reproduced. Read the cache under a seqlock or snapshot it as one unit.


## Fix

Closed with luc-knb1: cached_site takes the rewrite sequence before it reads the site and keeps what it read only if the sequence is even and the same afterwards, so the opcode, version, index and descriptor it hands the compiler are one rewrite's, however many came and went in between (specialize.h). The interpreter's readers had the same two-rewrite hole and take the same check.
