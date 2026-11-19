---
id: luc-dq13
status: closed
deps: []
links: []
created: 2026-11-19T11:09:07Z
type: task
priority: 2
---
# binascii base64 and hex codecs written for the compiler

Gap report 2026-11-18 mechanism #7, base64 row. binascii's b2a_base64/a2b_base64/hexlify/unhexlify ran per 3 bytes through slices, int.from_bytes, list comprehensions and bytes() of generators. Rewrite as index loops over bytes with lookup tables in bytes and stores into a preallocated bytearray (element intrinsics in line). a2b_base64 follows 3.14.10's padding rules (RFC 4648 3.3), which the old code did not.


## Notes

**2026-11-19T13:06:26Z**

Landed. Core lease, steady instructions per iteration (with luc-1dgu, against master ba3426d6): base64 0.677, pickle_pure_python 0.927, unpickle_pure_python 0.907. Kernel tests binascii_base64_padding (3.14.10 padding rules; failed on master), binascii_module; differential test against CPython's binascii over 66 k random inputs, strict and not.
