---
id: luc-afo6
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# xdsl: a threading.local subclass instance is unhashable

xdsl (xdsl/builder.py, _current_builder = _ThreadLocalBuilder()) fails at import: TypeError: cannot use 'weakref.ReferenceType' as a set element (unhashable type: '_ThreadLocalBuilder'). On CPython the instance hashes. Fails pyperformance xdsl. Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: threading.local keeps its weakrefs by id, so an unhashable (dataclass) subclass works. xdsl passes. tests/kernel/local_unhashable.py.
