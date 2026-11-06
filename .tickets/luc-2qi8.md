---
id: luc-2qi8
status: open
deps: []
links: []
created: 2026-11-06T01:54:05Z
type: task
priority: 3
assignee: Greg Wedow
---
# jit: aarch64 cross build of runtime/jit with the stub target

Last luc-d5u1 criterion. Needs an aarch64 gcc + sysroot (none on the build host). The native JIT_TARGET=stub build already prints 'no backend' and runs interpreted (94698e4); this checks the same thing under a real cross build.

