---
id: luc-1mcf
status: open
deps: []
links: []
created: 2026-11-03T09:38:16Z
type: task
priority: 3
tags: [kernel]
---
# typing: annotationlib (needs ast/_ast) and inspect are not importable, so alias reprs, get_type_hints, NamedTuple class syntax and runtime Protocol isinstance fail

import typing works under -I -S, but typing._type_repr, get_type_hints, class X(NamedTuple) with annotations and isinstance against a runtime_checkable Protocol import annotationlib (ast, enum) or inspect lazily. Repr of typing.List[int] needs them. Needs _ast/the compiler, enum and inspect frozen.

