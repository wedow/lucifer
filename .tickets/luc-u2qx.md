---
id: luc-u2qx
status: open
deps: []
links: []
created: 2026-11-11T23:37:00Z
type: bug
priority: 3
assignee: Greg Wedow
tags: [conformance]
---
# weakref != skips the referent's __ne__ on the native compare path

lu_compare_native answers != by negating eq_native; for two exact weakrefs weakref_eq (dict.c ~423) settles x==y when identity_known holds (checks only __hash__/__eq__). CPython's weakref_richcompare forwards op, so != runs the referent's __ne__. Repro: class A: def __ne__(s,o): return True; a=A(); {a:1}; r1,r2=weakref.ref(a,cb),weakref.ref(a,cb); r1 != r2 -> False here, True on CPython. Fix: decline (-1) for != unless __ne__ is object's. Add a test.

