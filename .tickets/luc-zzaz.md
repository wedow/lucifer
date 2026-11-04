---
id: luc-zzaz
status: open
deps: []
links: []
created: 2026-11-04T15:02:28Z
type: bug
priority: 3
---
# getattr, setattr and hasattr reject a str subclass as the attribute name

CPython accepts any str instance: getattr(obj, S('x')) with class S(str) returns obj.x. lib/kernel/builtins.py _attribute_name uses an exact-str test, so lucifer raises TypeError: attribute name must be string, not 'S'.

