---
id: luc-l0vb
status: open
deps: []
links: []
created: 2026-11-10T21:46:54Z
type: task
priority: 2
tags: [perf, jit]
---
# IOBase.closed and its __closed load generically in compiled code

On luc-custom4, BytesIO.closed (a property of _IOBase) and _IOBase__closed (a class attribute False, shadowed in an instance's dict once it is closed) are 50k-60k generic attribute loads in 3 loops of pickle_pure_python and unpickle_pure_python, though the interpreter specializes both sites (LOAD_ATTR_PROPERTY, LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES). The getter's site is shared by every IO class (FileIO's instances are closed, so the dict-lacks guard fails there), and the compiled site goes generic instead of keeping an arm per class.

