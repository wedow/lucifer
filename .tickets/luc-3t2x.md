---
id: luc-3t2x
status: open
deps: []
links: []
created: 2026-11-09T09:20:20Z
type: bug
priority: 2
assignee: Greg Wedow
tags: [perf]
---
# float benchmark 12% slower after luc-vxsk

pyperformance float: master a7c7208 65.0 ms, luc-webasync dbc5902 72.8 ms (CPython 3.14t 74.9), best of 3, seen three times. The branch ran fewer instructions on float, so allocation or memory behaviour is the suspect. Bisect the luc-vxsk commits on float and fix the mechanism.

