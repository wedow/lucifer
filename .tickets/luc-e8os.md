---
id: luc-e8os
status: closed
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


## Notes

**2026-11-21T16:58:58Z**

UTF-8 string construction and the codec primitive used separate decoders. The native decoder misreported the reason or byte span for several malformed sequences. Share validation and scan the ASCII prefix eight bytes at a time. Copy all-ASCII input directly into the compact string and start mixed-input validation at the first non-ASCII byte. This covers native construction, marshal, unpickling, bytes.decode, str construction, and incremental text decoding.

The new kernel check failed on the base. It passed with the fix alongside 12 nearby checks in each of the plain, JIT, and JIT-deopt lanes. The ABI audit passed.
