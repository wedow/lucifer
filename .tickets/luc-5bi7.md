---
id: luc-5bi7
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: reference counts out of compiled code (borrowed reads, paired counts removed, numbers without a kept box)

Measured: DECREF and INCREF are 6.7% of real-program steady state as IR instructions alone, before the counts inside LIST_ITEM, the stores and the release calls (_Py_Dealloc and the per-type deallocs another ~4%). In for v in xs: s += v the item is counted up when read and down one iteration later only so its box can be handed back if the int is ever needed as an object.

Mechanisms: (1) a value read from a container or attribute and used only until the next instruction that can run arbitrary code or switch tasks is borrowed, not counted (frees deferred past the next poll already exist for readers without the lock); (2) a count up and its matching count down with nothing between them that can observe the count cancel; (3) an unboxed int or float does not keep the object it was read from: ints in the small cache are immortal singletons already, and the owner-approved concession on re-boxed identity covers the rest only for attribute stores, so widening it is an owner decision to raise with numbers.

Acceptance: idiom rows list sum, tuple unpack, linked-list walk, read-4-attributes; DECREF+INCREF share on the 14 profiled pyperformance rows halved.

