---
id: luc-fg9t
status: open
deps: [luc-vvqv]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 3
parent: luc-nsmh
tags: [runtime, portability]
---
# L2: aarch64 interpreter lane

No arch #ifdef outside _Py_ThreadId and mimalloc; assembly under runtime/asm/<arch>/ (FOUNDATIONS 2.11). Cross toolchain and qemu-user are build/test packages to record in docs/validation.md.

## Acceptance Criteria

make ARCH=aarch64 CC=aarch64-linux-gnu-gcc builds bin/lucifer and make check-kernel passes under qemu-aarch64, interpreter only.

