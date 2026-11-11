---
id: luc-dh1h
status: open
deps: []
links: []
created: 2026-11-11T03:37:32Z
type: bug
priority: 3
tags: [compat]
---
# Slot wrappers' arity errors differ from CPython's

On master e2acbf0: int.__add__(3) raises "int.__add__() missing 1 required positional argument: 'other'" where CPython says "expected 1 argument, got 0"; int.__add__(3, 4, 5) "int.__add__() takes 2 positional arguments but 3 were given" (CPython "expected 1 argument, got 2"); (3).__lt__() and float.__mul__(1.0) the same. The runtime's own types answer their slots with kernel Python functions behind kernel wrappers, whose arity errors are a function's; CPython's slot wrappers check the argument count with check_num_args. str.__len__() matches. The kernel wrappers could check the count against the slot's wrapper kind before calling.

