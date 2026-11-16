---
id: luc-bxky
status: open
deps: []
links: []
created: 2026-11-16T14:32:30Z
type: bug
priority: 2
---
# multiprocessing test teardown hangs on the resource tracker

test_multiprocessing_{spawn,forkserver}.test_processes time out as whole modules: tearDownModule -> util._cleanup_tests -> resource_tracker._stop_locked blocks in waitpid on the tracker, which never sees EOF on its pipe, so some process still holds the write end. Same with spawn on master, so not forkserver-specific.

