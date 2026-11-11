---
id: luc-m58v
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Messages name types without their module where CPython uses %T/%N (set and dict keys, _io classes, csv, SimpleNamespace)

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_sethooks_diff (about 430 cases), pyio_text_diff (about 125), corelibs_diff (6) over seeds 1 and 2 at 1,000 cases each.

```python
class KH:
    def __hash__(self):
        raise TypeError('no')
KH.__module__ = 'somemod'
for f in (lambda: {KH()}, lambda: set().add(KH()), lambda: KH() in set(), lambda: {}.get(KH())):
    try:
        f()
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError cannot use 'somemod.KH' as a set element (no)
TypeError cannot use 'somemod.KH' as a set element (no)
TypeError cannot use 'somemod.KH' as a set element (no)
TypeError cannot use 'somemod.KH' as a dict key (no)
```
Lucifer:
```
TypeError cannot use 'KH' as a set element (no)
TypeError cannot use 'KH' as a set element (no)
TypeError cannot use 'KH' as a set element (no)
TypeError cannot use 'KH' as a dict key (no)
```

```python
import io
for o in (io.StringIO(), io.BytesIO()):
    try: o.name
    except Exception as e: print(type(e).__name__, e)
print(io.StringIO.__module__, io.BytesIO.__module__, io.TextIOWrapper.__module__)
```
CPython 3.14t:
```
AttributeError '_io.StringIO' object has no attribute 'name'
AttributeError '_io.BytesIO' object has no attribute 'name'
_io _io _io
```
Lucifer:
```
AttributeError 'StringIO' object has no attribute 'name'
AttributeError 'BytesIO' object has no attribute 'name'
_io _io _io
```

```python
import csv
class Sub(str):
    def __hash__(self): raise TypeError("boom")
Sub.__module__ = "m"
try: csv.register_dialect(Sub("x"))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError cannot use 'm.Sub' as a dict key (boom)
```
Lucifer:
```
TypeError cannot use 'Sub' as a dict key (boom)
```

```python
import calendar, argparse, types
try: list(calendar.Calendar().itermonthdates(-786150433475, 4))
except Exception as e: print(type(e).__name__, e)
try: types.SimpleNamespace().argparse
except Exception as e: print(type(e).__name__, e)
print(types.SimpleNamespace.__module__)
```
CPython 3.14t:
```
OverflowError signed integer is less than minimum
AttributeError 'types.SimpleNamespace' object has no attribute 'argparse'
types
```
Lucifer:
```
ValueError year must be in 1..9999, not -786150433475
AttributeError 'SimpleNamespace' object has no attribute 'argparse'
types
```

CPython formats the fully qualified name (module.qualname, builtins omitted) in these messages. calendar_overflow also shows an OverflowError vs ValueError difference.


