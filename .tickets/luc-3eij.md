---
id: luc-3eij
status: in_progress
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 2
tags: [perf, runtime]
---
# regex literal search through native find

_sre's _search_prefix and the generated single-character scans call bytes.find(int, ...) through kernel Python (argument parsing, a one-byte bytes per call): regex_effbot 6% + 6%. Route them to the native b_find/str_find primitives with needles prepared once. Census 2026-11-08 mechanism 6.

