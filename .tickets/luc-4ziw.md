---
id: luc-4ziw
status: open
deps: []
links: []
created: 2026-11-19T11:59:48Z
type: task
priority: 2
---
# a struct of one number packs to its bytes directly

pickle and base64 rows: pack('<I', n), pack('<B', n), packI in a85decode made a bytearray (type.__call__, not inlined), stored each byte under the bytearray's lock and copied it to bytes (bytes.__new__). A format whose one item is a number filling the struct returns int.to_bytes / the float's bytes directly.

