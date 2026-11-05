---
id: luc-fban
status: open
deps: []
links: []
created: 2026-11-05T16:31:07Z
type: bug
priority: 3
assignee: Greg Wedow
---
# kernel **kwargs forwarding drops str-subclass keyword keys

Left from luc-kil1. Kernel builtins that forward **kwargs pass exact str keys where CPython keeps the caller's subclass objects: a class call into a user __init__, dict(**...), dict subclasses, functools.partial. Kernel type constructors also match keywords by text where CPython's tp_call hands C code a dict looked up by hash+__eq__ (int('7', **{LyingStr('base'): 8}) binds on lucifer, refused on CPython). '{x}'.format(**{Name('x'): 1}) raises KeyError on lucifer, works on CPython. Root: the kernel's keyword parsing assumes exact-str keys (~181 **kw definitions under lib/); fix at the parsing layer, not per site. Low priority: only matters for str-subclass keyword names.

