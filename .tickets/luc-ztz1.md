---
id: luc-ztz1
status: open
deps: [luc-00js]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 3
parent: luc-nsmh
tags: [runtime, observability]
---
# L3: remote inspection: _Py_DebugOffsets, sys.remote_exec

_PyRuntime starts with a _Py_DebugOffsets (cookie xdebugpy, section .PyRuntime) describing Lucifer's task and frame layouts; the remote-exec poll bit (FOUNDATIONS 2.5, 2.12).

## Acceptance Criteria

tools/libtest run --modules test_external_inspection at oracle parity (24 PASS); sys.remote_exec from a second process runs a script in the target.

