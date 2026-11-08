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


## Notes

**2026-11-08T19:52:12Z**

Landed on luc-hotc: _search_prefix and the scans of '.' and of a negated literal call b_find (memmem) or str_find with needles prepared once per pattern; str_find tests the first code unit before comparing. regex_effbot 13.25x -> 4.42x CPython, regex_v8 7.68x -> 6.70x. Test: re_literal_search.py.
