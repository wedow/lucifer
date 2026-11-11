---
id: luc-3snm
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# datetime (_pydatetime) messages and C-int argument checks differ from C _datetime

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): datetime_diff (about 130 cases), datetime_zoneinfo_diff fromutc overflow (131 cases) over seeds 1 and 2 at 1,000 cases each.

```python
import datetime as d
for f in (lambda: d.date(9999, 12, 31) + d.timedelta(1),
          lambda: d.datetime(9999, 12, 31) + d.timedelta(1),
          lambda: d.datetime(1, 1, 1) - d.timedelta(1),
          lambda: d.timedelta(days=999999999) + d.timedelta(1),
          lambda: d.timedelta(days=999999999) * 2,
          lambda: d.timedelta(weeks=-2**31),
          lambda: d.timedelta(days=2**70),
          lambda: d.timedelta(days=1e30)):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
OverflowError date value out of range
OverflowError date value out of range
OverflowError date value out of range
OverflowError days=1000000000; must have magnitude <= 999999999
OverflowError days=1999999998; must have magnitude <= 999999999
OverflowError Python int too large to convert to C int
OverflowError Python int too large to convert to C int
OverflowError Python int too large to convert to C int
```
Lucifer:
```
OverflowError result out of range
OverflowError result out of range
OverflowError result out of range
OverflowError timedelta # of days is too large: 1000000000
OverflowError timedelta # of days is too large: 1999999998
OverflowError timedelta # of days is too large: -15032385536
OverflowError timedelta # of days is too large: 1180591620717411303424
OverflowError timedelta # of days is too large: 1000000000000000019884624838656
exit 0
```

```python
import datetime as d
for f in (lambda: d.date.fromisoformat(',7082\U0001F600'),
          lambda: d.date.fromisoformat('2020-867\ud800'),
          lambda: d.date.fromisoformat(2),
          lambda: d.time.fromisoformat('24:01'),
          lambda: d.time.fromisoformat('24:00'),
          lambda: d.time.fromisoformat('T4०'),
          lambda: d.datetime.fromisoformat('0400-08-01Z')):
    try: print(repr(f()))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError Invalid isoformat string: ',7082😀'
ValueError Invalid isoformat string: '2020-867\ud800'
TypeError fromisoformat: argument must be str
ValueError minute, second, and microsecond must be 0 when hour is 24
datetime.time(0, 0)
ValueError Invalid isoformat string: 'T4०'
ValueError Invalid isoformat string: '0400-08-01Z'
```
Lucifer:
```
ValueError Argument must be an ASCII str
ValueError Argument must be an ASCII str
TypeError Argument must be a str
ValueError Invalid isoformat string: '24:01'
datetime.time(0, 0)
ValueError Invalid isoformat string: '4०'
datetime.datetime(400, 8, 1, 0, 0)
exit 0
```

```python
import datetime as d
class Z(d.tzinfo): pass
n = d.datetime(2000, 1, 1)
a = d.datetime(2000, 1, 1, tzinfo=d.timezone.utc)
for f in (lambda: d.datetime.now(tz=1),
          lambda: a.astimezone(5),
          lambda: n < a,
          lambda: n - a,
          lambda: d.time(1) < d.time(1, tzinfo=d.timezone.utc),
          lambda: d.time(1) - d.time(1),
          lambda: d.time(1, tzinfo=Z()) < d.time(2, tzinfo=d.timezone.utc),
          lambda: d.date(2000, 1, 1) - d.datetime(2000, 1, 1),
          lambda: d.datetime(*[1] * 10)):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError tzinfo argument must be None or of a tzinfo subclass, not type 'int'
TypeError tzinfo argument must be None or of a tzinfo subclass, not type 'int'
TypeError can't compare offset-naive and offset-aware datetimes
TypeError can't subtract offset-naive and offset-aware datetimes
TypeError can't compare offset-naive and offset-aware times
TypeError unsupported operand type(s) for -: 'datetime.time' and 'datetime.time'
NotImplementedError a tzinfo subclass must implement utcoffset()
TypeError unsupported operand type(s) for -: 'datetime.date' and 'datetime.datetime'
TypeError function takes at most 9 arguments (10 given)
```
Lucifer:
```
TypeError tzinfo argument must be None or of a tzinfo subclass, not 'int'
TypeError tz argument must be an instance of tzinfo
TypeError cannot compare naive and aware datetimes
TypeError cannot mix naive and timezone-aware time
TypeError cannot compare naive and aware times
TypeError unsupported operand type(s) for -: 'time' and 'time'
NotImplementedError tzinfo subclass must override utcoffset()
0:00:00
TypeError datetime.__new__() takes from 2 to 9 positional arguments but 11 were given
exit 0
```

```python
import datetime as d
for f in (lambda: d.datetime(2000, 1, 1).replace(month=-2**31 - 1),
          lambda: d.datetime(2000, 1, 1).replace(fold=2**31),
          lambda: d.datetime(2000, 1, 1).replace(hour=-2**31 - 1, minute=2**31 - 1),
          lambda: d.date.fromordinal(2**63),
          lambda: d.date.fromisocalendar(1, 2**64, 5),
          lambda: d.time(2**31),
          lambda: type(d.date(2000, 1, 1).isocalendar())(-2**31 - 1, 99, 2**31)):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
OverflowError Python int too large to convert to C int
OverflowError Python int too large to convert to C int
OverflowError Python int too large to convert to C int
OverflowError Python int too large to convert to C long
ValueError ISO calendar component out of range
OverflowError signed integer is greater than maximum
OverflowError Python int too large to convert to C int
```
Lucifer:
```
ValueError month must be in 1..12, not -2147483649
ValueError fold must be either 0 or 1, not 2147483648
ValueError hour must be in 0..23, not -2147483649
ValueError year must be in 1..9999, not 25252734927766555
ValueError Invalid week: 18446744073709551616
ValueError hour must be in 0..23, not 2147483648
IsoCalendarDate(year=-2147483649, week=99, weekday=2147483648)
exit 0
```

```python
import datetime as d
for f in (lambda: d.time(0, 59, 59, 999999).replace(hour=None, second=2020),
          lambda: d.time(2**31, 24, -1, b"x"),
          lambda: d.timedelta(1) * 1.5):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError 'NoneType' object cannot be interpreted as an integer
OverflowError signed integer is greater than maximum
1 day, 12:00:00
```
Lucifer:
```
ValueError second must be in 0..59, not 2020
TypeError 'bytes' object cannot be interpreted as an integer
1 day, 12:00:00
exit 0
```

_datetime is `from _pydatetime import *`. Also: date - datetime returns 0:00:00 instead of TypeError, and fromisoformat('0400-08-01Z') is accepted.


