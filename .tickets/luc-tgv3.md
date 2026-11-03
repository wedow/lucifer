---
id: luc-tgv3
status: open
deps: [luc-vvqv]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 1
parent: luc-nsmh
tags: [runtime, subinterpreters]
---
# K1.1: check-kernel in a second interpreter (-X lucifer_subinterp)

Runtime policy flag: initialize the main interpreter, create a second with Py_NewInterpreterFromConfig (isolated config), run the program there, end it, finalize. Static builtin types keep tp_dict/tp_subclasses per interpreter; kernel functions reached via tstate->interp (FOUNDATIONS 2.1).

## Acceptance Criteria

make check-kernel passes under -X lucifer_subinterp, plain and with -X lucifer_handoff; the oracle's NULL tp_dict for static builtin types holds (tests/capi read of PyLong_Type.tp_dict is NULL).

