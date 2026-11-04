---
id: luc-zj23
status: open
deps: []
links: []
created: 2026-11-04T11:33:22Z
type: feature
priority: 2
assignee: Greg Wedow
---
# _socket: full socket API through the syscall bracket

Only socketpair I/O and getaddrinfo/gethostbyname exist (luc-c702). Implement socket(), bind/listen/accept/connect, send/recv variants, setsockopt/getsockopt, timeouts (settimeout via lu_park + poll), makefile, as CPython's Modules/socketmodule.c. Every blocking call goes through the syscall bracket (blockcheck enforces). Acceptance: test_socket imports and runs; PASS set within 90% of the oracle's on plain and handoff lanes.

