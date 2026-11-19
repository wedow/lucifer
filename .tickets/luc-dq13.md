---
id: luc-dq13
status: open
deps: []
links: []
created: 2026-11-19T11:09:07Z
type: task
priority: 2
---
# binascii base64 and hex codecs written for the compiler

Gap report 2026-11-18 mechanism #7, base64 row. binascii's b2a_base64/a2b_base64/hexlify/unhexlify ran per 3 bytes through slices, int.from_bytes, list comprehensions and bytes() of generators. Rewrite as index loops over bytes with lookup tables in bytes and stores into a preallocated bytearray (element intrinsics in line). a2b_base64 follows 3.14.10's padding rules (RFC 4648 3.3), which the old code did not.

