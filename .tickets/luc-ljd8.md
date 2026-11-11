---
id: luc-ljd8
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Uninitialized and detached TextIOWrapper objects behave differently

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_text_diff, about 70 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
t = io.TextIOWrapper.__new__(io.TextIOWrapper)
for n in ("buffer", "closed", "seekable", "writable", "isatty", "name", "flush", "close", "detach", "tell", "_CHUNK_SIZE"):
    try:
        v = getattr(t, n); print(n, v() if callable(v) else v)
    except Exception as e: print(n, type(e).__name__, e)
```
CPython 3.14t:
```
buffer ValueError I/O operation on uninitialized object
closed True
seekable ValueError I/O operation on uninitialized object
writable ValueError I/O operation on uninitialized object
isatty ValueError I/O operation on uninitialized object
name ValueError I/O operation on uninitialized object
flush ValueError I/O operation on uninitialized object
close ValueError I/O operation on uninitialized object
detach ValueError I/O operation on uninitialized object
tell ValueError I/O operation on uninitialized object
_CHUNK_SIZE ValueError I/O operation on uninitialized object
```
Lucifer:
```
buffer None
closed ValueError underlying buffer has been detached
seekable ValueError underlying buffer has been detached
writable AttributeError 'NoneType' object has no attribute 'writable'
isatty AttributeError 'NoneType' object has no attribute 'isatty'
name AttributeError 'NoneType' object has no attribute 'name'
flush ValueError underlying buffer has been detached
close None
detach ValueError buffer is already detached
tell AttributeError 'TextIOWrapper' object has no attribute '_seekable'
_CHUNK_SIZE 8192
```

```python
import io
t = io.TextIOWrapper(io.BytesIO(b"abc"))
t.detach()
for n in ("closed", "seekable", "flush", "detach", "name", "writable", "isatty"):
    try:
        v = getattr(t, n); print(n, v() if callable(v) else v)
    except Exception as e: print(n, type(e).__name__, e)
```
CPython 3.14t:
```
closed ValueError underlying buffer has been detached
seekable ValueError underlying buffer has been detached
flush ValueError underlying buffer has been detached
detach ValueError underlying buffer has been detached
name ValueError underlying buffer has been detached
writable ValueError underlying buffer has been detached
isatty ValueError underlying buffer has been detached
```
Lucifer:
```
closed ValueError underlying buffer has been detached
seekable ValueError underlying buffer has been detached
flush ValueError underlying buffer has been detached
detach ValueError buffer is already detached
name AttributeError 'NoneType' object has no attribute 'name'
writable AttributeError 'NoneType' object has no attribute 'writable'
isatty AttributeError 'NoneType' object has no attribute 'isatty'
```

CPython raises ValueError "I/O operation on uninitialized object" before __init__ ran.


