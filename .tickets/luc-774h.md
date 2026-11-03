---
id: luc-774h
status: open
deps: []
links: []
created: 2026-11-03T09:48:24Z
type: feature
priority: 0
parent: luc-66sy
tags: [compiler, kernel]
---
# M0.0a: _ast node classes, ast module, runtime compile()/exec/eval of source through lib/compiler, -c and .py scripts

_ast classes generated from Parser/Python.asdl as in Python/Python-ast.c (fields, _attributes, defaults, __match_args__, repr, compare), lib/compiler running on bin/lucifer for compile(), exec/eval of str, -c, and running .py files (writing __pycache__ pycs as CPython). Revert the namedtuple eval() patch in third_party/cpython/CHANGES.md. Unblocks annotationlib/inspect (luc-1mcf).

## Acceptance Criteria

tests/kernel programs for compile/exec/eval of source, ast.parse+ast.dump (include_attributes), ast.unparse, -c, and a .py script match the oracle; compile() output marshal-identical to the oracle for the difftest corpus when run under bin/lucifer

