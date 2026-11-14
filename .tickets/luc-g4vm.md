---
id: luc-g4vm
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [startup, stdlib]
---
# site is never imported at startup

bin/lucifer leaves config.site_import unused: site is not in sys.modules after startup, so builtins has no exit, quit, copyright, help, and site-packages .pth files never run. A multiprocessing spawn child calling exit(100) dies with NameError (test_cprofile TestCommandLine.test_process_spawn_pickle). CPython 3.14t imports site unless -S, also under -I.

