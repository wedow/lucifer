---
id: lr-ojw0
status: open
deps: []
links: []
created: 2026-11-04T05:31:44Z
type: bug
priority: 2
---
# Set add and discard commit by table index after an unlocked probe; lu_ns_lookup and _PyType_Lookup return borrowed values


## Notes

**2026-11-04T05:31:57Z**

Left by luc-rjiv. Set add/discard still commit by table index (set_discard_at, set_insert) after a lock-free probe: two threads adding one key can store it twice, and a resize between probe and discard can drop the wrong key. Fix as luc-n6cx did for dicts (stamp or native probe under the critical section). Also lu_ns_lookup (68 callers) and _PyType_Lookup hand out borrowed values from dicts other threads may write; convert callers to lu_ns_get where the dict is shared. lu_ns_next iterates unlocked. Page-level QSBR is now on for the object heaps.
