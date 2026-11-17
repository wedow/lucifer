---
id: luc-eih1
status: open
deps: []
links: []
created: 2026-11-17T07:54:44Z
type: bug
priority: 2
---
# capi: dict_watch_resurrect_run differs under -X lucifer_jit=always

On master (55b5f8c and 101cb05) tests/capi/dict_watch_resurrect_run.py prints the oracle's output with default flags but not with -X lucifer_jit=always: the watcher sees different values ('a', 2)/('a', 3) where the oracle has ('a', 1)/('a', 0). make check-capi LUCIFER_FLAGS='-X lucifer_jit=always' fails on it.

