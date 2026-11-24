---
id: luc-0wyz
status: open
deps: []
links: []
created: 2026-11-24T19:18:43Z
type: bug
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# Python-written runtime modules read rebindable builtins

import builtins; builtins.len = lambda x: 42; print('abc') fails in Lucifer (IndexError, then OSError: write() returned incorrect number of bytes): the Python-written _io looks len up through the builtins module a program can rebind; CPython's C _io does not. The same holds for every kernel module and accelerator port that reaches a builtin by name. Decide the general rule (kernel and port modules bind the original builtins at build or boot, e.g. their globals' __builtins__ is a private frozen namespace) and test it by rebinding each of the common builtins (len, isinstance, type, range, str, int, list, dict, tuple, getattr, hasattr, iter, next, min, max, sum, any, all, repr, print) and running a program that prints, formats, sorts, imports and does file I/O. Found by the interpreter work (luc-2r1l).

