---
id: luc-j89m
status: open
deps: []
links: []
created: 2026-11-17T04:59:54Z
type: task
priority: 2
assignee: Greg Wedow
---
# polymorphic call switch: per-context callees without a rebuild per callee

Branch luc-perf-j (454336e switch, f17a37f, ff99457 per-context callees) not landed: per-context learning fixes dead arms (json_loads +2.4% -> +0.2%) but each new callee triggers a rebuild, so compile cost dominates (AWFY DeltaBlue compile +44%, code +39%; pp deltablue +3.4%). Next: learn without one rebuild per callee, then count-weighted arm room. jitcheck polycalls lane fails on f17a37f too. Notes: perf-j/wk/HANDOFF-j2.txt.

