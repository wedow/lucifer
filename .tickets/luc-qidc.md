---
id: luc-qidc
status: open
deps: [luc-xljo]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1n: config, sys, frozen importlib and builtin modules

Py_BytesMain is PyConfig parsing (Python/initconfig.c semantics for -I, -S, -E, -B, -u, -X opt[=v], PYTHONHASHSEED, a .pyc or script path; -c and source scripts need the compiler and stay luc-66sy) then Py_InitializeFromConfig then Py_RunMain. One -X lucifer_stress=KIND[:N][,...][,seed:S] parser into _PyRuntime (known kinds gc, deopt; inert until their rungs; unknown kind is a startup error with exit 2) and -X lucifer_handoff[=SEED] parsed into _PyRuntime for K1r. lib/kernel/sys.py populates sys (version_info (3,14,10,'final',0), abiflags 't', implementation incl. cache_tag and lucifer, flags incl. hash_randomization, argv, orig_argv, _xoptions, path, modules, exit, getrecursionlimit/setrecursionlimit, intern, getrefcount, _is_gil_enabled, exception/exc_info, _getframe placeholder for K1s). Frozen importlib._bootstrap and _bootstrap_external installed as CPython does, IMPORT_NAME/IMPORT_FROM through builtins.__import__, lib/modules ports needed under -I -S: _imp (frozen and builtin surface), _thread (get_ident, get_native_id, allocate_lock over PyMutex, _local; no thread start), _weakref (ref/proxy over a weakref list, no cycles), _warnings absent, errno, posix (minimal: getcwd, fspath, stat, environ, listdir, write/read through the bracket), gc (enable/disable/isenabled/collect returning 0, get_threshold, callbacks list), atexit, _string, _signal stub, time (time, monotonic, perf_counter, sleep through lu_park deadline). SystemExit at top level gives CPython's exit codes and messages. _Py_HashSecret comes from PyConfig.
Implements: DESIGN 3.3, 7 (sys values), 9.3 (startup row up to stdio); FOUNDATIONS 2.7 (parser), 2.13 (Py_BytesMain order), 2.15 (_imp surface), 2.16.
Files: runtime/main.c, runtime/interp/config.c, lib/kernel/sys.py, lib/modules/{_imp,_thread,_weakref,errno,posix,gc,atexit,_string,_signal,time}.py, tools/freeze (frozen set grows).
Adds tests/kernel: hash_seed.py, sys_exit.py, import_builtin.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="hash_seed sys_exit import_builtin" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t) with PYTHONHASHSEED=12345 and =0 for hash_seed; two unseeded runs print different hash('abc') and sys.flags.hash_randomization == 1; bin/lucifer -X lucifer_stress=bogus exits 2 with a message (a kernelcheck flag row).

