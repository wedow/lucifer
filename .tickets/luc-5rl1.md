---
id: luc-5rl1
status: open
deps: []
links: []
created: 2026-11-10T10:36:14Z
type: bug
priority: 2
parent: luc-zelv
---
# cli and startup slower after kernel customization

Corpus on the bench host, master (cd44643) -> luc-custom (1a04f48), ratio to CPython 3.14t: cli geomean 6.74 -> 7.19 (isort 1.17x, markdown 1.11x, pyflakes 1.08x of master), python_startup 1.13x, python_startup_no_site 1.09x, concurrent_imap 1.20x, while pyperformance went 3.65 -> 3.42. Suspects: the interpreter's new class notes (every CALL of a builtin or kernel function with an argument of no kind, every FOR_ITER over another iterator, every unary op) on cold code, and compile time from operator and call arms. Measure with -X lucifer_jit_stats phase seconds and an interpreter-only run.

