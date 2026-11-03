---
id: luc-sex3
status: open
deps: [luc-66sy]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 3
parent: luc-nsmh
tags: [runtime, startup]
---
# L1: startup-image readiness: deterministic kernel init

All init-time heap state rooted in the state records, address- and seed-independent init, no str hashes in frozen data (FOUNDATIONS 2.10). Keeps the DESIGN 10 image experiment open.

## Acceptance Criteria

-X lucifer_heapdigest prints a digest of the post-init object graph (types, shapes, values, edges; no addresses); three runs with ASLR and PYTHONHASHSEED=1 and =2 print the same digest.

