---
id: luc-e8os
status: open
deps: []
links: []
created: 2026-11-21T16:42:42Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# str from UTF-8 decodes byte at a time: no ASCII fast path

luc-tax0 row 3: lu_str_from_utf8 is 20% of unpickle steady cycles, decoding a byte at a time. Add a word-at-a-time ASCII prefix scan (8 bytes per step, high-bit mask), falling back to the existing decoder at the first non-ASCII byte. General: every bytes.decode('utf-8'), file read and str construction from UTF-8 benefits. Check the other UTF-8 entry points share it. Measure unpickle, json_loads and one text-reading row.

