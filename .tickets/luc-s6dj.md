---
id: luc-s6dj
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# weakref.ref to a bound method raises TypeError

weakref.ref(obj.method) raises TypeError: cannot create weak reference to 'method' object; CPython method objects are weakly referenceable. SQLAlchemy's event registry (event/registry.py) does this, failing pyperformance sqlalchemy_declarative and sqlalchemy_imperative. Corpus: tools/bench/corpus.

