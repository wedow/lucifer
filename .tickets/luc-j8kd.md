---
id: luc-j8kd
status: open
deps: []
links: []
created: 2026-11-06T05:03:09Z
type: bug
priority: 3
---
# Instance-attribute stores tell dict watchers MODIFIED even for the same value

CPython stores an object's attributes into its split-table __dict__ through insert_split_value, which sends PyDict_EVENT_MODIFIED unconditionally; a combined dict (insertdict) sends it only when the value changes. lucifer has no split tables and sends it only on change (lu_dict_replace), so test_capi.test_watchers TestDictWatchers.test_object_dict (100 stores of 'bar' expect 99 'mod' events) fails. The instance-dict store sites are lu_attr_store (runtime/object/attrs.c), STORE_ATTR_WITH_HINT (runtime/interp/ceval.c), lu_setattr_native (runtime/object/descr.c) and the JIT's h_attr_store (runtime/jit/helpers.c); all four would need the split-table rule, and lu_ns_store is shared with class and module namespaces, which keep the combined rule.

