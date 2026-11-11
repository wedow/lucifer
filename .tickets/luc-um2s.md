---
id: luc-um2s
status: open
deps: []
links: []
created: 2026-11-11T13:55:19Z
type: bug
priority: 2
assignee: Greg Wedow
tags: [conformance]
---
# ctypes arrays report format 'i' and empty arrays fail memoryview()

A ctypes c_int array's buffer format is 'i'; CPython reports '<i'. memoryview() of an empty ctypes array raises 'mem must not be NULL'; CPython gives an empty view.

