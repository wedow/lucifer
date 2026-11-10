---
id: luc-6scb
status: open
deps: []
links: []
created: 2026-11-10T19:21:54Z
type: task
priority: 1
tags: [perf, jit]
---
# A megamorphic FOR_ITER over a Python iterator runs about 20x CPython

A kernel loop whose iterable type differs across calls (str.join's [x for x in iterable], list(), tuple(), set() over iterables) compiles next_item generically: next_missing_of as a prim call (a linear registry scan, 6% of the micro), type_lookup of __next__, call_special through h_call, the callee entered by its direct entry, plus the try/except StopIteration. A loop that joins a 300-item deque after str.join has seen eleven iterable types costs 217 ns per item against CPython's 10 ns (0.196 s against 0.009 s for 3000 joins), on master and on luc-custom3 alike.

This is what mako lost on luc-custom3: with the poll serving merges and collections in place (luc-ar8c), the generic str.join@1 unit never leaves its loop, so the interpreter never OSR-compiles str.join@126 with the deque iterator's __next__ inlined, which base did after its 15 poll exits. mako per loop 6282 -> 7635 (x100k instructions), 816k generic __next__ calls in 6 loops, wall 1.24x master.

Levers, general: (1) version a compiled loop on its iterator's exact type at the loop's entry when the type is unknown, so each iterable type gets the inlined step (the loop-invariant type, not the site's profile, which next_item shares with every caller); (2) at least make the generic step cheap: the registry lookup O(1) and folded where the type is known only at run time, and the __next__ call as a cached direct call keyed on the type.

