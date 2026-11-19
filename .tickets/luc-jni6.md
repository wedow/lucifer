---
id: luc-jni6
status: open
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: bug
priority: 3
---
# struct: native 8-byte codes out of range say argument out of range

struct.pack('@q', 2**63) raises struct.error 'argument out of range'; CPython's native table (np_longlong etc.) says "'q' format requires -9223372036854775808 <= number <= 9223372036854775807"; the standard modes say 'argument out of range'. _struct._int picks the message by size and code only, not by mode. Waits for luc-nvdz (the shift in _int).

