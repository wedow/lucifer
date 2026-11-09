---
id: luc-zelv
status: open
deps: []
links: []
created: 2026-11-09T08:58:06Z
type: epic
priority: 1
---
# Kernel customization: per-site specialized kernel code, operators as sends, fused iteration

Kernel builtins and operators are generic Python dispatchers shared by every call site; their feedback is merged and many opcodes cannot appear in inlined frames, so ordinary idioms run 2.5-15x slower than CPython 3.14t. Build the Smalltalk-style properties: customization (kernel code specialized per call site from that site's feedback), operators as sends with class feedback, iteration builtins fused into the consuming loop, and a kernel library written for the compiler. Design: docs/design/kernel-customization.md.

