---
id: luc-5rl1
status: closed
deps: []
links: []
created: 2026-11-10T10:36:14Z
type: bug
priority: 2
parent: luc-zelv
---
# cli and startup slower after kernel customization

Corpus on the bench host, master (cd44643) -> luc-custom (1a04f48), ratio to CPython 3.14t: cli geomean 6.74 -> 7.19 (isort 1.17x, markdown 1.11x, pyflakes 1.08x of master), python_startup 1.13x, python_startup_no_site 1.09x, concurrent_imap 1.20x, while pyperformance went 3.65 -> 3.42. Suspects: the interpreter's new class notes (every CALL of a builtin or kernel function with an argument of no kind, every FOR_ITER over another iterator, every unary op) on cold code, and compile time from operator and call arms. Measure with -X lucifer_jit_stats phase seconds and an interpreter-only run.


## Notes

**2026-11-10T13:03:29Z**

Fixed on luc-custom2 (74c5c68, 09166cf). Causes, by instructions of importing json, re, argparse and typing: class notes ran on all code with JIT state (callee notes give nearly every code state), every new class pair invalidated its dependents (16 recompiles against 5 before customization), and call arms in units of kernel code (getattr_hook serves every caller) left on 50 guard failures each. Now a site has a class table only after a build asked for it (the first pair recompiles, later ones go through guard exits), operand kinds are computed once per operator, and units compiled for kernel code take no call arms outside FOR_ITER steps. 811M on master -> 746M (733M before customization; the rest is the interpreter running the larger kernel, the same with the JIT off). cli+startup geomean ratio to CPython: 7.45 master, 7.04 branch, 6.91 before customization.
