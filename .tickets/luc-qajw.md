---
id: luc-qajw
status: open
deps: []
links: []
created: 2026-11-11T02:46:59Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Assorted library differences: str * huge negative, NoneType.__doc__, unquote errors handler, plist messages, base64 __buffer__ errors

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff and textlibs_diff, 1 case each over seeds 1 and 2 at 1,000 cases each.

```python
for f in (lambda: "a".translate({}, 1), lambda: "a".__mul__(-2**63-1), lambda: "a" * (-2**63-1)):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError str.translate() takes exactly one argument (2 given)
OverflowError cannot fit 'int' into an index-sized integer
OverflowError cannot fit 'int' into an index-sized integer
```
Lucifer:
```
TypeError str.translate() takes 2 positional arguments but 3 were given
```

```python
import types
print(repr(types.NoneType.__doc__))
```
CPython 3.14t:
```
'The type of the None singleton.'
```
Lucifer:
```
None
```

```python
import urllib.parse
try: print(urllib.parse.unquote("%%%FG%abe", "utf-8", "xmlcharrefreplace"))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError don't know how to handle UnicodeDecodeError in error callback
```
Lucifer:
```
UnicodeDecodeError 'utf-8' codec can't decode byte 0xab in position 5: invalid start byte
```

```python
import plistlib, datetime
d = datetime.datetime(1, 1, 1, tzinfo=datetime.timezone(datetime.timedelta(seconds=-86399)))
try: print(plistlib.dumps(d, fmt=plistlib.FMT_BINARY))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError can't subtract offset-naive and offset-aware datetimes
```
Lucifer:
```
TypeError cannot mix naive and timezone-aware time
```

```python
import plistlib
try: plistlib.dumps("\ud800\ud800a", fmt=plistlib.FMT_BINARY)
except Exception as e: print(type(e).__name__, e)
try: plistlib.dumps({"a": "€𐏿"}, fmt=plistlib.FMT_BINARY)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
UnicodeEncodeError 'utf-16-be' codec can't encode character '\ud800' in position 0: surrogates not allowed
```
Lucifer:
```
UnicodeEncodeError 'utf-16-be' codec can't encode characters in position 0-1: surrogates not allowed
```

```python
import base64
class BF:
    def __buffer__(self, f): raise TypeError("boom")
for fn in (base64.urlsafe_b64encode, base64.b64encode, base64.b16encode):
    try: fn(BF())
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError boom
TypeError boom
TypeError boom
```
Lucifer:
```
TypeError a bytes-like object is required, not 'BF'
TypeError a bytes-like object is required, not 'BF'
TypeError a bytes-like object is required, not 'BF'
```

Separate small causes, batched.


