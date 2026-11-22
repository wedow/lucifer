---
id: luc-b568
status: open
deps: []
links: []
created: 2026-11-22T04:04:34Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# compiled code calls int_mul and rangeiter_next prims out of line

luc-tax0 follow-up: after aabbc3e7 and 36ecfdd0, the next prim family by count from compiled code is async_tree's int_mul (18M per run) and rangeiter_next (18M). Lower them in compiled units the way aabbc3e7 lowered size and seq_get: inline the small-int multiply with an overflow check, falling back to the prim, and advance range iterators inline when the iterator's type is known. First count prim calls per primitive across rows (with a counting build) and take the top two families, in case they differ from these. General mechanism only.

