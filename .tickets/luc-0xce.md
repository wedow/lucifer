---
id: luc-0xce
status: open
deps: []
links: []
created: 2026-11-24T19:18:43Z
type: bug
priority: 3
assignee: Greg Wedow
parent: luc-orn7
---
# __import__ is looked up in the interpreter's builtins, not the frame's

lu_import_name (runtime/interp/ceval.c) looks __import__ up in the interpreter's builtins; CPython uses the frame's __builtins__. exec('def f():\n import os\n return os', {'__builtins__': {'__import__': lambda *a: 42}}) then f() returns 42 on CPython and the os module on Lucifer, in the interpreter and (since the import opcodes are compiled as helper calls sharing that function) in compiled code. Pass the frame's builtins (the unit's, in compiled code) into the shared function; kernel test with a custom builtins dict, a builtins module, and a missing __import__ (ImportError: __import__ not found).

