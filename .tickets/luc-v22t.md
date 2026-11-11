---
id: luc-v22t
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# reversed(dict.items()) yields an item after popitem() and a new insert

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff op it_next, 1 case over seeds 1 and 2 at 1,000 cases each.

```python
P = [2305843009213693951, 208, 88, 0, 7, 1048576, 18, 8796093022208, -1, 240, 128,
     2305843009213693952, 9223372036854775807]
d = {}
d[P[0]] = P[9]
d[P[10]] = P[1]
d[P[12]] = P[11]
del d[P[10]]
d[P[7]] = P[10]
d[P[2]] = P[4]
d[P[12]] = P[12]
d.popitem()
d[P[12]] = P[5]
it = reversed(d.items())
print(len(d), it.__length_hint__())
d.popitem()
print(len(d), it.__length_hint__())
d[P[5]] = P[5]
try:
    print(next(it))
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
3 3
2 0
StopIteration 
```
Lucifer:
```
3 3
2 0
(1048576, 1048576)
```


