---
id: luc-cybz
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# zoneinfo (Python ZoneInfo): TZ footer messages, non-UTF-8 footers, version-1 offsets and fromutc at datetime.min differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_zoneinfo_diff fns zi_c, zi_py, about 120 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io, zoneinfo, datetime as d
data = bytes([84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 60, 43, 57, 62, 43, 47, 50, 10])
try:
    z = zoneinfo.ZoneInfo.from_file(io.BytesIO(data))
except Exception as e:
    print(type(e).__name__, e)
    raise SystemExit
for q in []:
    dt = d.datetime(*q[:6], fold=q[6])
    print("q", q, z.utcoffset(dt), z.dst(dt), z.tzname(dt))
for q in []:
    dt = d.datetime(*q[:6], fold=q[6], tzinfo=z)
    try:
        r = z.fromutc(dt)
        print("u", q, r.replace(tzinfo=None) - dt.replace(tzinfo=None), r.fold)
    except Exception as e:
        print("u", q, type(e).__name__, e)
```
CPython 3.14t:
```
ValueError Invalid STD offset in b'<+9>+/2'
```
Lucifer:
```
ValueError <+9>+/2 is not a valid TZ string
exit 0
```

```python
import io, zoneinfo, datetime as d
data = bytes([84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 105, 43, 115, 108, 226, 169, 122, 90, 10])
try:
    z = zoneinfo.ZoneInfo.from_file(io.BytesIO(data))
except Exception as e:
    print(type(e).__name__, e)
    raise SystemExit
for q in []:
    dt = d.datetime(*q[:6], fold=q[6])
    print("q", q, z.utcoffset(dt), z.dst(dt), z.tzname(dt))
for q in []:
    dt = d.datetime(*q[:6], fold=q[6], tzinfo=z)
    try:
        r = z.fromutc(dt)
        print("u", q, r.replace(tzinfo=None) - dt.replace(tzinfo=None), r.fold)
    except Exception as e:
        print("u", q, type(e).__name__, e)
```
CPython 3.14t:
```
ValueError Invalid STD offset in b'i+sl\xe2\xa9zZ'
```
Lucifer:
```
UnicodeDecodeError 'utf-8' codec can't decode bytes in position 4-5: invalid continuation byte
exit 0
```

```python
import io, zoneinfo, datetime as d
data = bytes([84, 90, 105, 102, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 7, 192, 167, 185, 11, 1, 0, 0, 28, 32, 1, 5, 0, 0, 14, 16, 1, 5, 67, 69, 83, 84, 0, 88, 0, 72, 9])
try:
    z = zoneinfo.ZoneInfo.from_file(io.BytesIO(data))
except Exception as e:
    print(type(e).__name__, e)
    raise SystemExit
for q in [[1936, 4, 28, 16, 40, 58, 1], [1970, 11, 19, 14, 59, 59, 1], [1900, 11, 4, 12, 30, 0, 0]]:
    dt = d.datetime(*q[:6], fold=q[6])
    print("q", q, z.utcoffset(dt), z.dst(dt), z.tzname(dt))
for q in [[1936, 4, 28, 16, 40, 58, 0], [1970, 11, 19, 14, 59, 59, 0], [1900, 11, 4, 12, 30, 0, 0]]:
    dt = d.datetime(*q[:6], fold=q[6], tzinfo=z)
    try:
        r = z.fromutc(dt)
        print("u", q, r.replace(tzinfo=None) - dt.replace(tzinfo=None), r.fold)
    except Exception as e:
        print("u", q, type(e).__name__, e)
```
CPython 3.14t:
```
q [1936, 4, 28, 16, 40, 58, 1] 2:00:00 1:00:00 X
q [1970, 11, 19, 14, 59, 59, 1] 1:00:00 1:00:00 X
q [1900, 11, 4, 12, 30, 0, 0] 2:00:00 1:00:00 X
u [1936, 4, 28, 16, 40, 58, 0] 1:00:00 1
u [1970, 11, 19, 14, 59, 59, 0] 1:00:00 0
u [1900, 11, 4, 12, 30, 0, 0] 2:00:00 0
```
Lucifer:
```
q [1936, 4, 28, 16, 40, 58, 1] 1:00:00 1:00:00 X
q [1970, 11, 19, 14, 59, 59, 1] 1:00:00 1:00:00 X
q [1900, 11, 4, 12, 30, 0, 0] 1:00:00 1:00:00 X
u [1936, 4, 28, 16, 40, 58, 0] 1:00:00 0
u [1970, 11, 19, 14, 59, 59, 0] 1:00:00 0
u [1900, 11, 4, 12, 30, 0, 0] 1:00:00 0
exit 0
```

```python
import io, zoneinfo, datetime as d
data = bytes([84, 90, 105, 102, 57, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 90, 105, 102, 57, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 7, 0, 0, 0, 0, 12, 183, 222, 173, 0, 0, 0, 0, 13, 93, 198, 120, 0, 0, 0, 0, 21, 233, 118, 218, 1, 1, 1, 0, 0, 0, 0, 1, 0, 255, 255, 87, 64, 1, 1, 76, 77, 84, 0, 65, 66, 0, 10, 10])
z = zoneinfo.ZoneInfo.from_file(io.BytesIO(data))
for q in [[1976, 10, 5, 16, 18, 5, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0], [1970, 10, 11, 10, 30, 0, 0, 0], [1981, 8, 25, 22, 10, 50, 0, 0], [1977, 2, 7, 11, 31, 4, 0, 0], [7729, 8, 25, 22, 5, 9, 0, 0]]:
    dt = d.datetime(*q[:6], fold=q[6], tzinfo=z)
    try:
        r = z.fromutc(dt)
        print("u", q, r.replace(tzinfo=None) - dt.replace(tzinfo=None), r.fold)
    except Exception as e:
        print("u", q, type(e).__name__, e)
```
CPython 3.14t:
```
u [1976, 10, 5, 16, 18, 5, 0, 0] -1 day, 12:00:00 1
u [1, 1, 1, 0, 0, 0, 0, 0] 0:00:00 0
u [1970, 10, 11, 10, 30, 0, 0, 0] 0:00:00 0
u [1981, 8, 25, 22, 10, 50, 0, 0] -1 day, 12:00:00 0
u [1977, 2, 7, 11, 31, 4, 0, 0] -1 day, 12:00:00 0
u [7729, 8, 25, 22, 5, 9, 0, 0] -1 day, 12:00:00 0
```
Lucifer:
```
u [1976, 10, 5, 16, 18, 5, 0, 0] -1 day, 12:00:00 0
u [1, 1, 1, 0, 0, 0, 0, 0] OverflowError result out of range
u [1970, 10, 11, 10, 30, 0, 0, 0] -1 day, 12:00:00 0
u [1981, 8, 25, 22, 10, 50, 0, 0] -1 day, 12:00:00 0
u [1977, 2, 7, 11, 31, 4, 0, 0] -1 day, 12:00:00 0
u [7729, 8, 25, 22, 5, 9, 0, 0] -1 day, 12:00:00 0
exit 0
```


