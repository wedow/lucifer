---
id: luc-zpty
status: open
deps: []
links: []
created: 2026-11-03T08:13:00Z
type: task
priority: 2
parent: luc-vvqv
---
# K1n follow-ups: zipimport and the frozen startup set

zipimport is not installed in sys.path_hooks, sys.path has no stdlib directory, sys.stdlib_module_names and sys.getsizeof are absent, and the frozen set is only the importlib bootstrap (DESIGN 3.3 lists codecs, encodings, io, abc, site, os, posixpath, stat, genericpath, _collections_abc, _sitebuiltins). Needed by M0.0 imports.py.

