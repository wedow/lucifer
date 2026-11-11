---
id: luc-w6ht
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Buffered I/O after detach() raises AttributeError instead of 'raw stream has been detached'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 80 cases over seeds 1 and 2 at 1,000 cases each.

```python
# after detach() the buffered object reports ValueError, not AttributeError on None
import io
b = io.BufferedReader(io.BytesIO(b"abc"))
b.detach()
for name in ("closed", "name", "mode"):
    try:
        print(name, getattr(b, name))
    except Exception as e:
        print(name, type(e).__name__, e)
try:
    b.read()
except Exception as e:
    print("read", type(e).__name__, e)
```
CPython 3.14t:
```
closed ValueError raw stream has been detached
name ValueError raw stream has been detached
mode ValueError raw stream has been detached
read ValueError raw stream has been detached
```
Lucifer:
```
closed AttributeError 'NoneType' object has no attribute 'closed'
name AttributeError 'NoneType' object has no attribute 'name'
mode AttributeError 'NoneType' object has no attribute 'mode'
read AttributeError 'NoneType' object has no attribute 'read'
```


