---
id: luc-m6gq
status: open
deps: []
links: []
created: 2026-11-04T12:56:43Z
type: bug
priority: 2
parent: luc-h4gc
---
# sys._current_frames misses a running thread; a second thread starves

A daemon thread that sleeps 20 ms and reads sys._current_frames().get(main_ident) gets None for the main thread, and ticks only twice during a 3M-iteration busy loop in the main thread (CPython: a frame every tick). Blocks sampling profilers and thread-based watchdogs.

