---
id: luc-abar
status: open
deps: [luc-qidc]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1t: versions, watchers and inline caches

ABI version fields with CPython's meaning: tp_version_tag assignment and lu_type_modified (= PyType_Modified, recursive over tp_subclasses, called from kernel type.__setattr__, __bases__ assignment and MRO changes), tp_watched bits, _ma_watcher_tag and lu_dict_notify on every namespace and kernel dict mutation, func_version with lu_func_notify (f.__code__, defaults), code create/destroy events. Inline caches in the CACHE words, rewritten with atomic stores (valid under concurrent execution): LOAD_GLOBAL (globals/builtins keys versions), LOAD_ATTR (instance value, class attribute, method, module), STORE_ATTR instance value, CALL to Python function (func_version), BINARY_OP compact int add/sub and float, COMPARE_OP int, FOR_ITER list/range/tuple, TO_BOOL exact types; each guard deopts to the generic opcode. Specialized forms stay invisible except through dis(adaptive=True).
Implements: DESIGN 6.1 (inline caches), 10 (shared co_code rewrite); FOUNDATIONS 2.4.
Files: runtime/interp/specialize.c, runtime/prim/type.c (version tags, watchers).
Adds tests/kernel: invalidate.py (warm a loop, then mutate a class attribute, a base attribute, __bases__, a global, builtins.len, f.__code__ and obj.__class__; each result printed).

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="invalidate" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); every earlier program still matches; the handoff lanes for it are checked in K1u.

