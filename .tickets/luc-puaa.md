---
id: luc-puaa
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _pickle accelerator is missing; five pickle benchmarks fail

pyperformance's pickle, pickle_dict, pickle_list, unpickle and unpickle_list benchmarks require the C pickle (RuntimeError: Missing C accelerators for pickle); only pickle_pure_python and unpickle_pure_python run. Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _pickle is the tag's Modules/_pickle.c (pickle.py's Python classes as _pickle timed out pickle_dict and pickle_list). Needed _Py_ID as interned strings, _PyBytesWriter and pickle.PickleBuffer as a runtime type. All five benchmarks pass. test_pickle 932/1012 (CPython 967); the rest fail in the Python pickler too (method_descriptor and singleton-type reduction, compat mappings).
