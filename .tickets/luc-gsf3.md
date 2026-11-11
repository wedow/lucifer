---
id: luc-gsf3
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 1
tags: [conformance, fuzzdiff]
---
# zoneinfo: a TZif file with a huge count hangs or crashes Lucifer

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_zoneinfo_diff, 1 case over seeds 1 and 2 at 1,000 cases each (exit -11 in the fuzz run, a 60 s timeout standalone).

```python
import io, zoneinfo, datetime as d
data = bytes([84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 90, 105, 102, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0, 127, 255, 255, 255, 0, 0, 0, 2, 0, 0, 0, 9, 0, 0, 0, 0, 1, 4, 0, 0, 28, 32, 0, 4, 69, 83, 84, 0, 67, 69, 83, 84, 0, 211, 205, 158, 54, 10, 76, 58, 100, 65, 45, 49, 54, 56, 87, 65, 110, 104, 57, 57, 57, 58, 53, 57, 44, 74, 51, 54, 53, 47, 50, 53, 44, 74, 49, 48, 48])
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
error unpack requires a buffer of 17179869176 bytes
```
Lucifer:
```
exit 124
```

CPython: struct.error "unpack requires a buffer of 17179869176 bytes".


