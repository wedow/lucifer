---
id: luc-oof7
status: open
deps: []
links: []
created: 2026-11-21T05:39:27Z
type: bug
priority: 1
assignee: Greg Wedow
---
# libtest: a shard leaves forkserver/manager children alive after it ends or times out

The 11-19 and 11-20 daily floors left five test scopes alive for a day (3 processes each): test_multiprocessing_forkserver/spawn shards 108, 110, 112 of 538 under -X lucifer_stress=deopt:7. tools/libtest/run.py exited but the multiprocessing forkserver and manager processes the tests started stayed, so the scope never ended. The runner must kill every process a shard started (its process group or session, and anything still in it) when the shard ends or hits --timeout, normally or not. Test: a harness test whose fake worker starts a detached grandchild that ignores SIGTERM; after the shard returns, the grandchild is gone.

