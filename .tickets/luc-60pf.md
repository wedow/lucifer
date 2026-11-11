---
id: luc-60pf
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# datetime: hash of date/time, repr of nested subclasses and IsoCalendarDate, astimezone with a caching __new__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_diff fns date_hash, tm_hash, x3_repr, x2_dt_repr_cls, iso_repr, x6_astz, about 24 cases over seeds 1 and 2 at 1,000 cases each.

```python
import datetime as d
x = d.date(4975, 7, 31)
data = bytes([4975 >> 8, 4975 & 255, 7, 31])
print(hash(x) == hash(data), hash(x) == hash((data,)))
class Z(d.tzinfo):
    def utcoffset(self, dt): return d.timedelta(seconds=60)
    def dst(self, dt): return None
    def tzname(self, dt): return None
t = d.time(0, 59, 59, 843410, tzinfo=Z())
print(hash(t) == hash(d.timedelta(0, 3539, 843410)))
class Z2(Z):
    def utcoffset(self, dt): return d.timedelta(microseconds=500000)
try: print(hash(d.time(12, 59, 30, 500000, tzinfo=Z2())))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
True False
True
-5935115414643097953
```
Lucifer:
```
False True
False
AssertionError whole minute
exit 0
```

```python
import datetime as d
class Outer:
    class DTSub(d.datetime): pass
    class TSub(d.timedelta): pass
print(repr(Outer.DTSub(2000, 1, 1)))
print(repr(Outer.TSub(1)))
print(repr(d.date(2000, 1, 1).isocalendar()))
print(type(d.date(2000, 1, 1).isocalendar()).__module__)
```
CPython 3.14t:
```
DTSub(2000, 1, 1, 0, 0)
TSub(days=1)
datetime.IsoCalendarDate(year=1999, week=52, weekday=6)
datetime
```
Lucifer:
```
Outer.DTSub(2000, 1, 1, 0, 0)
Outer.TSub(days=1)
IsoCalendarDate(year=1999, week=52, weekday=6)
datetime
exit 0
```

```python
import datetime as d
cache = []
class D(d.datetime):
    armed = False
    def __new__(cls, *a, **k):
        if D.armed and len(a) == 8:
            return cache[0]
        return d.datetime.__new__(cls, *a, **k)
tz = lambda us: d.timezone(d.timedelta(microseconds=us))
cache.append(D(2065, 5, 3, 1, 0, 0, 0, tzinfo=None))
v = D(1917, 5, 27, 0, 49, 27, 568520, tzinfo=tz(-1))
D.armed = True
try: r = v.astimezone(tz(-1)); print(r)
except Exception as e: print(type(e).__name__, e)
print(cache[0])
```
CPython 3.14t:
```
2065-05-03 01:00:00
2065-05-03 01:00:00
```
Lucifer:
```
ValueError fromutc: dt.tzinfo is not self
2065-05-03 01:00:00
exit 0
```


