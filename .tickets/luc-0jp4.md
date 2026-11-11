---
id: luc-0jp4
status: open
deps: []
links: []
created: 2026-11-11T20:53:04Z
type: bug
priority: 2
assignee: Greg Wedow
tags: [threads]
---
# set/dict walks read the source one entry at a time without its lock

set intersection, difference, issubset, isdisjoint, dict ==, dict repr and items ^ walk the source entry by entry and can skip keys when another thread resizes it mid-walk. CPython holds the source's critical section for these walks. Use the native merge pattern from luc-setmerge (both sections held, real __eq__ inside).

