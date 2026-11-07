---
id: luc-5ds3
status: closed
deps: []
links: []
created: 2026-11-07T01:22:27Z
type: task
priority: 2
assignee: Greg Wedow
---
# DeltaBlue and Json 1.5-4% slower after luc-ajdn

luc-ajdn (exceptions in compiled code) made DeltaBlue 1.022-1.037x and Json 1.015-1.019x of master in two AWFY runs; two variants of in-unit catching did not recover DeltaBlue. Compare instruction counts and jit stats master^ vs master on both; find the mechanism.


## Notes

**2026-11-07T03:07:55Z**

Not reproduced: instruction counts equal (DeltaBlue +0.08%, Json same, pinned/ASLR off); jit stats equal apart from intended error-exit drop; timing spread between layout-only builds is 3-4%. Cause of the reported 1.5-4%: code layout (L1i +2.5%, DSB misses +4%).
