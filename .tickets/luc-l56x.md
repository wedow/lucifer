---
id: luc-l56x
status: open
deps: []
links: []
created: 2026-11-25T11:57:10Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# collection is 11% of async_tree: attribute it

From the item 2 work (01a9cae0): 11.1% of async_tree's cycles are in collection (visit_mark_alive, lu_gc_alloc), from many short-lived Task, Future and Handle objects. Establish first why these reach the collector at all: which of them are in reference cycles (Task and its coroutine, Future and its callbacks, Handle and its context), how often a collection runs against how much it frees, and what CPython 3.14t does on the same program (collections and time in them). Related: luc-uu0y (collection triggered from resident size). No fix proposed until the attribution exists.

Acceptance: a table of collections, objects visited and freed per run for the async_tree family on both runtimes, and the mechanism named.

