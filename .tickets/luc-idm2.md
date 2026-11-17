---
id: luc-idm2
status: closed
deps: []
links: []
created: 2026-11-17T07:57:36Z
type: task
priority: 2
---
# C-API: answer container calls without Python where no key needs it

builtins.compile was 10.4% of import_stdlib instructions, 6x CPython per compile: CPython's compiler calls the C-API, and PyList_Sort, PyDict_Keys, PySet_New, PyNumber_InPlaceOr, PySet_Add/Contains/Discard, PyDict_GetItemRef/SetItem, PyObject_Hash and PyUnicode_DecodeUTF8Stateful each ran kernel Python (and JIT builds of it). Use the native paths the interpreter already has (lu_contains_native, lu_dict_find/put_native, lu_set_add_simple, lu_set_merge, lu_hash_native) and a native sort for lists of exact strs; fall back to the kernel where a key needs Python.


## Notes

**2026-11-17T08:09:11Z**

Landed 7f0f7d2. instructions:u (min of 5, core lease), 101cb05 -> 7f0f7d2: import_stdlib 2277.7 -> 2122.6 M (-6.8%), isort 3842.4 -> 3725.9 M (-3.0%), markdown 4576.4 -> 4282.6 M (-6.4%), pygments 17045 -> 16748 M (-1.7%). Test tests/capi/native_containers_run.py (in check-capi-ext). Gates: floor quick off PASS after rerun (test_threads Barrier.test_default_timeout lost under load, passes alone), jit PASS, deopt PASS; check-capi default and off, check-harness pass. Left under builtins.compile: PySequence/kernel calls in analyze_child_block (PyObject_CallOneArg), PyList_AsTuple, PyObject_GetIter in update_symbols, PyImport_ImportModule from codec paths with a consumed count.
