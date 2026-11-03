---
id: luc-vvqv
status: open
deps: [luc-dfbu]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
assignee: Greg Wedow
parent: luc-nsmh
tags: [runtime, kernel, slice]
---
# kernel: interpreter, primitives and Python builtins run host-compiled pyc under -I -S

runtime/interp (ceval over 3.14 opcodes, frames, exceptions, generators, class creation, native marshal reader, frozen loader), runtime/prim (_lucifer primitives), runtime/thread locks, lib/kernel builtins installed into the static types, startup through the _io port and encodings, tools/freeze (docs/DESIGN.md 4.4, 6.1, 9.3). tests/kernel programs are written first and shown failing.

## Acceptance Criteria

make check-kernel: every tests/kernel program (at least 30, covering the families in docs/DESIGN.md 9.3) run as a host-compiled .pyc with bin/lucifer -I -S matches the cpython-3.14.10t oracle on stdout, stderr and exit status.

