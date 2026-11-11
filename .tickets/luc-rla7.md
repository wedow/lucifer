---
id: luc-rla7
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# list += and list.extend do not call __length_hint__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, 36 cases over seeds 1 and 2 at 1,000 cases each.

```python
class It:
    n = 0
    def __iter__(self): return self
    def __next__(self):
        print("next"); self.n += 1
        if self.n > 2: raise StopIteration
        return self.n
    def __length_hint__(self): print("hint"); return 2
l = [1]
l += It()
print(l)
l = [1]
l.extend(It())
print(list(It()), len([*It()]))
```
CPython 3.14t:
```
hint
next
next
next
[1, 1, 2]
hint
next
next
next
hint
next
next
next
hint
... (4 more lines)
```
Lucifer:
```
next
next
next
[1, 1, 2]
next
next
next
next
next
next
next
next
next
[1, 2] 2
```


