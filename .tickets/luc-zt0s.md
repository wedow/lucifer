---
id: luc-zt0s
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# datetime: subclass constructors receive fold=0 from replace, astimezone, combine and fromisoformat

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_diff fns x5_*, x4_replsub, about 18 cases over seeds 1 and 2 at 1,000 cases each.

```python
import datetime as d
class S(d.datetime):
    def __new__(cls, *a, **k):
        if S.armed: print("new", len(a), sorted(k))
        return d.datetime.__new__(cls, *a, **k)
S.armed = False
v = S(2000, 4, 1, 12, tzinfo=d.timezone.utc)
S.armed = True
v.replace(month=5)
v.astimezone(d.timezone(d.timedelta(hours=1)))
S.combine(d.date(2000, 1, 1), d.time(1))
S.fromisoformat('2000-01-01T01:00')
class T(d.time):
    def __new__(cls, *a, **k):
        if "fold" in k and not S.armed2: pass
        if S.armed2: print("T new", len(a), sorted(k))
        return d.time.__new__(cls, *a, **k)
S.armed2 = False
t = T(1, 2); S.armed2 = True
t.replace(minute=3)
T.fromisoformat('01:02')
```
CPython 3.14t:
```
new 8 []
new 8 []
new 8 []
new 8 []
new 8 []
T new 5 []
T new 5 []
```
Lucifer:
```
new 8 ['fold']
new 8 ['fold']
new 8 ['fold']
new 8 ['fold']
new 8 ['fold']
new 8 []
T new 5 ['fold']
T new 5 []
exit 0
```

C passes fold only when it is nonzero.


