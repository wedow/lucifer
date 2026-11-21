---
id: luc-g4vm
status: closed
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [startup, stdlib]
---
# site is never imported at startup

bin/lucifer leaves config.site_import unused: site is not in sys.modules after startup, so builtins has no exit, quit, copyright, help, and site-packages .pth files never run. A multiprocessing spawn child calling exit(100) dies with NameError (test_cprofile TestCommandLine.test_process_spawn_pickle). CPython 3.14t imports site unless -S, also under -I.


## Notes

**2026-11-21T04:27:17Z**

Startup now honors site_import after streams and __main__ are initialized. The command path enters sys.path after site runs, so startup hooks see the same path order as CPython. The regression went red before the change and matches CPython 3.14t for default startup, -S, -I, user .pth execution, and fatal startup errors. Targeted plain, jit, jit-deopt, ABI, embedding, and cProfile spawn checks passed. The stdio checks used the C locale due to a baseline difference under C.UTF-8.
