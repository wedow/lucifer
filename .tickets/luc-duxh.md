---
id: luc-duxh
status: open
deps: []
links: []
created: 2026-11-04T17:16:05Z
type: feature
priority: 1
assignee: Greg Wedow
---
# replace lib/compiler with CPython's C compiler, byte-identical

The Python port of CPython's compiler (lib/compiler) is byte-identical but runs ~70x slower than CPython's C compiler on large inputs and dominates short-lived startup (traceback import, dataclasses, namedtuple, pytest rewrite). The Python compiler was a bootstrap convenience (DESIGN decision 3); the compiler is not on the speed path. Vendor CPython 3.14's tokenizer (Parser/lexer, Parser/tokenizer), generated parser (Parser/parser.c, action_helpers, pegen.c, string_parser), AST (Python/Python-ast.c, _ast), and Python/{symtable,ast_preprocess,codegen,compile,flowgraph,assemble,instruction_sequence}.c into runtime/, adapting CPython-internal helpers to Lucifer's equivalents. Keep _tokenize and _ast as CPython has them in C. Update DESIGN.md decision 3.

Acceptance: make check-compiler stays 100% byte-identical on all corpora (now through the C compiler); make check, difftest, abi-audit green; lib/compiler deleted (or kept only if a tool needs it, with the reason); check-timing: 100,000-line function under 5 s, uncaught exception exit under 0.15 s; numpy test_multiarray.py compiles in under 1 s.


## Notes

**2026-11-04T19:55:42Z**

Done on branch luc-duxh: runtime/compiler vendors CPython 3.14.10's compiler (lexer, pegen parser, AST, symtable, codegen, flowgraph, assembler; _ast, _tokenize, _symtable, _opcode as C modules); lib/compiler, genast, opcodegen deleted. check-compiler 100% on closure, difftest, kernel and lib (1873 files); make check, abi-audit, difftest 164/164 green; compile_threads passes in plain and handoff lanes. Idle host: uncaught exit 0.71 s -> 0.17 s (target 0.15 not met: compile is 11% of it, the rest is interpreting the traceback import), 100k-line function 126 s -> 1.2 s, numpy test_multiarray.py 18.6 s -> 0.52 s.
