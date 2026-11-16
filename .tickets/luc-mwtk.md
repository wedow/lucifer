---
id: luc-mwtk
status: open
deps: []
links: []
created: 2026-11-16T11:32:48Z
type: task
priority: 1
assignee: Greg Wedow
---
# per-byte loops in binascii and _struct run 100-474x slower than C

base64 7.3x: binascii base64/hex are per-byte Python loops (b16_large 474x, b64_large 309x); _struct ~100x slower (a85encode 85% one struct call); binascii.unhexlify ~140x. These are JIT targets by policy (stdlib stays Python): add ladder rows for a per-byte bytes/bytearray loop and a struct.pack call and diagnose per-item cost vs PyPy; bytes.fromhex for unhexlify is fine as a Python-level change.

