---
id: luc-8j0x
status: open
deps: []
links: []
created: 2026-11-11T19:49:34Z
type: bug
priority: 3
assignee: Greg Wedow
tags: [conformance]
---
# object.__new__(Sub) names the static base, not the subclass, in its error

class MyList(list): pass; object.__new__(MyList) says 'use list.__new__()' where CPython says 'use MyList.__new__()'. check_new's static-base loop skips every heap type.

