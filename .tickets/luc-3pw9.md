---
id: luc-3pw9
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# type.__type_params__ on type itself is a getset_descriptor instead of ()

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, 35 cases over seeds 1 and 2 at 1,000 cases each.

```python
print(type.__type_params__)
print(int.__type_params__)
class A: pass
print(A.__type_params__)
```
CPython 3.14t:
```
()
()
()
```
Lucifer:
```
<getset_descriptor object at 0x...>
()
()
```


