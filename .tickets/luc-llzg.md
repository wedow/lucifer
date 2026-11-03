---
id: luc-llzg
status: closed
deps: []
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
parent: luc-nsmh
tags: [compiler, slice]
---
# compiler: Python bytecode compiler byte-identical to CPython 3.14 on the host

Port tokenizer, pegen parser (python.gram with Python actions), _ast, symtable, AST preprocess, codegen, flowgraph and assemble to lib/compiler/ (docs/DESIGN.md 3.2, 9.1). Developed and checked on the host CPython 3.14 via tools/compilercheck and make check-compiler.

## Acceptance Criteria

make check-compiler: marshal.dumps and ast.dump(include_attributes=True) byte-identical to the host for the 42 Python modules of the difftest probe+bridge import closure, all 164 difftest programs and tests/kernel; the run reports the identical share of all vendored Lib/**/*.py the host compiles.


## Notes

**2026-11-03T02:37:45Z**

Merged. make check-compiler: closure 42/42, difftest 198/198, Lib 1868/1868, byte-identical at all five stages vs CPython 3.14.10.
