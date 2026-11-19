---
id: luc-4t4e
status: closed
deps: []
links: [luc-t6j2]
created: 2026-11-19T19:00:00Z
type: bug
priority: 2
tags: [jit, threads]
---
# a unit killed before compile_at publishes its direct entry keeps the entry after a collection frees it

Found reading jit.c for luc-t6j2. compile() registers a new unit's dependencies; compile_at then links the unit and stores its direct entry. A writer that kills the unit in between marks it dead and tries to take the entry back with a compare-and-swap that finds it not set yet; compile_at stores it anyway, and the next collection that finds no activation in the unit frees it and its code while jc->direct.entry still points there: a C caller then jumps into freed or reused code.

## Fix

compile_at stores the entry, then reads the unit's dead flag and takes the entry back itself if the unit died; kill sets the flag, then takes back the entry it finds. Both pairs are seq_cst, so one of the two sees the other before compile_at clears the compiling flag a collection waits for. Not reproduced: master 1929ea93 showed no crash in 140 runs of jit_reclaim_threads (default flags) nor in 40 of a program built to race exactly this (direct entries compiled under a writer that kills them and a collector), so there is no regression test beyond jit_reclaim_threads.
