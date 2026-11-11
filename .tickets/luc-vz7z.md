---
id: luc-vz7z
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# datetime: subclass overrides (isoformat, __reduce_ex__, strftime %z on date, timedelta * int subclass) honoured differently

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_diff fns x4_str, x5_str, x6_reduce, x6_strfz, date_strftime, x4_mulsub, x4_tdobj, about 14 cases over seeds 1 and 2 at 1,000 cases each.

```python
import datetime as d
class D(d.date):
    def isoformat(self): return "X"
class T(d.time):
    def isoformat(self, timespec="auto"): return "X"
class DT(d.datetime):
    def isoformat(self, *a, **k): return "P" + ",".join(map(str, a)) + "K" + ",".join(k)
print(str(D(2000, 1, 1)), str(T(1)), str(DT(2000, 1, 1)))
```
CPython 3.14t:
```
X X P K
```
Lucifer:
```
2000-01-01 01:00:00 PKsep
exit 0
```

```python
import datetime as d
class S(d.time):
    def __reduce_ex__(self, p): return (str, ("custom", p))
print(S(1, 2).__reduce__()[0])
class T(d.timedelta): pass
x = T(1)
print(x.__reduce__()[0] is T)
```
CPython 3.14t:
```
<class '__main__.S'>
True
```
Lucifer:
```
<class 'str'>
True
exit 0
```

```python
import datetime as d
class S(d.date):
    def utcoffset(self): return d.timedelta(microseconds=-1)
    def tzname(self): return "OV"
print(repr(S(2020, 8, 18).strftime("%z|%Z")))
print(repr(d.date(2004, 8, 17).strftime("%:%:z")))
```
CPython 3.14t:
```
'|'
'%:'
```
Lucifer:
```
'-000000.000001|OV'
'%:%:z'
exit 0
```

```python
import datetime as d
class I(int):
    def __mul__(s, o): return 2.5
try: print(d.timedelta(1) * I(1))
except Exception as e: print(type(e).__name__, e)
print(d.timedelta(days=0, seconds=0) is d.timedelta(0))
```
CPython 3.14t:
```
TypeError 'float' object cannot be interpreted as an integer
True
```
Lucifer:
```
1 day, 0:00:00
False
exit 0
```


