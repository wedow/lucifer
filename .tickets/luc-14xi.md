---
id: luc-14xi
status: closed
deps: []
links: []
created: 2026-11-17T08:39:00Z
type: task
priority: 2
---
# capi: utf-8 PyUnicode_AsEncodedString without a str.encode call

Every path argument (os.stat, os.open) was encoded by calling the kernel's str.encode through a method call. Without surrogates every handler gives the cached UTF-8. os.stat 33k -> 19k instructions/op.

