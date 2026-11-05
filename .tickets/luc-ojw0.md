---
id: luc-ojw0
status: closed
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

**2026-11-04T23:53:12Z**

Set add/discard/contains commit in one critical section naming judged candidates by identity (prims set_add_entry, set_discard_entry); _PyType_LookupRef returns a reference (cache hit by try-incref), _PyType_Lookup stays borrowed for identity compares; deref callers in ceval, slots, descr, errdisplay, protocols converted; inline-cache descriptors try-incref'd. Remaining borrowed cache pointers: luc-y0g5.
