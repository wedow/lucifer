---
id: luc-5ds3
status: open
deps: []
links: []
created: 2026-11-07T01:22:27Z
type: task
priority: 2
assignee: Greg Wedow
---
# DeltaBlue and Json 1.5-4% slower after luc-ajdn

luc-ajdn (exceptions in compiled code) made DeltaBlue 1.022-1.037x and Json 1.015-1.019x of master in two AWFY runs; two variants of in-unit catching did not recover DeltaBlue. Compare instruction counts and jit stats master^ vs master on both; find the mechanism.

