---
id: luc-tgqa
status: in_progress
deps: []
links: []
created: 2026-11-18T19:01:26Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: compiled code enters a compiled callee's unit directly

Gap report 2026-11-18 mechanism #2. A call compiled code does not take in line goes h_call -> call_function -> lu_frame_push -> lu_eval -> lu_jit_run -> unit, about 500 instructions of protocol per entry on Richards (7.5 M entries per iteration). f9c8284 dropped the old middle end's direct entry (d6c8996d, f29908ef). Restore it on the new middle end: the call site tests for a Python function (or bound method of one) whose code has a call-entry unit taking exactly the call's positional arguments, and calls that unit's direct entry, which makes the interpreter's call and RESUME checks, pushes and binds the frame in line, links the activation, and runs the unit; the callee pops its frame at return, and one that leaves compiled code hands its frame to lu_jit_call_finish.

## Acceptance Criteria

Richards and the pyperformance rows the gap report names (richards, raytrace, json_loads, pickle/unpickle_pure_python, deltablue, chaos, go) lose instructions per iteration on a core lease; tools/floor quick, kernelcheck plain/jit/jit-deopt and check-jit pass.

