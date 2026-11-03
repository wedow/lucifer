---
id: luc-xljo
status: closed
deps: [luc-d47e, luc-fha1, luc-r5qm, luc-5w74]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1m: builtins module functions

lib/kernel/builtins.py complete for the K1 surface, ported from PyPy pypy/module/__builtin__/app_*.py and CPython Python/bltinmodule.c semantics: abs, all, any, ascii, bin, callable, chr, delattr, dir, divmod, enumerate, filter, format, getattr, globals, hasattr, hash, hex, id, isinstance, issubclass (with __instancecheck__/__subclasscheck__), iter (incl. callable-sentinel form), len, locals, map, max, min, next, oct, ord, pow, print (sep, end, file, flush; file=None means sys.stdout once K1o lands, fd 1 until then), repr, reversed, round, setattr, sorted, sum (exact-int and float fast paths, fsum-free CPython semantics), vars, zip (strict), exec/eval of code objects (strings wait for the runtime compiler in luc-66sy), __import__ placeholder, NotImplemented/Ellipsis names. Every function is a kernel function (method_descriptor/builtin_function_or_method type, hidden frames) so tracebacks from sorted/map/dict.__getitem__ show only user frames.
Implements: DESIGN 4.4 (builtins); FOUNDATIONS 2.8 (exceptions through kernel builtins).
Files: lib/kernel/builtins.py, lib/kernel/iterators.py (map/filter/zip/enumerate/reversed types).
Adds tests/kernel: builtins_funcs.py, print_args.py, exc_kernel.py (raises through sorted/dict.__getitem__/map with chaining, sys.exception() in generators, except*).

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="builtins_funcs print_args exc_kernel" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T07:35:34Z**

K1m done: builtins functions, iterators.py, callable_iterator; builtins_funcs, print_args, exc_kernel match the oracle; 356/356 kernel programs.
