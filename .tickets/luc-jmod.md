---
id: luc-jmod
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# float(x) does not return the object __float__ returned, and a float subclass's __float__ is ignored

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): binary64_diff fn o_new, 4 cases over seeds 1 and 2 at 1,000 cases each.

```python
r = 1.5e300
class C:
    def __float__(self): return r
print(float(C()) is r)
```
CPython 3.14t:
```
True
```
Lucifer:
```
False
```

```python
class FS(float):
    def __float__(self): return 2.5
print(float(FS(1.0)))
import math
print(math.nextafter(FS(1.0), FS(1.0)))
```
CPython 3.14t:
```
2.5
1.0
```
Lucifer:
```
1.0
1.0
```


