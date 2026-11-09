---
id: luc-7qkn
status: open
deps: []
links: []
created: 2026-11-09T02:01:52Z
type: task
priority: 2
parent: luc-vxsk
tags: [perf]
---
# list slice assignment (x[:] = []) runs kernel Python from compiled code

From luc-vxsk: asyncio Future.__schedule_callbacks clears its callbacks with self._callbacks[:] = []; STORE_SUBSCR/STORE_SLICE of an exact list with a slice goes h_setitem -> kernel setitem -> list.__setitem__, 2.5% of async_tree. A native path for exact lists (PyList_SetSlice is still a stub) would serve every slice store.

