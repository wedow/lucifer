---
id: luc-mb5s
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# repr of self-referential containers nests one level deeper

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, 24 cases over seeds 1 and 2 at 1,000 cases each.

```python
a = []
t = (a,)
a.append(t)
print(repr(a), repr(t))
l = [t]
a.append(l)
print(repr(a))
d = {}
d[1] = [d]
print(repr(d))
```
CPython 3.14t:
```
[([...],)] ([(...)],)
[([...],), [([...],)]]
{1: [{...}]}
```
Lucifer:
```
[([...],)] ([([...],)],)
[([...],), [([...],)]]
{1: [{...}]}
```


