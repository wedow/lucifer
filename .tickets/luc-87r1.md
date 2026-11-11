---
id: luc-87r1
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# IOBase _check* helpers take a message argument; RawIOBase.write/readinto raise UnsupportedOperation

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_base_diff (about 130), pyio_text_diff (about 50) over seeds 1 and 2 at 1,000 cases each.

```python
import io
class S(io.RawIOBase): pass
s = S()
for n in ("_checkClosed", "_checkSeekable", "_checkReadable", "_checkWritable"):
    try: print(n, getattr(s, n)("m"))
    except Exception as e: print(n, type(e).__name__, e)
for n, a in (("write", (b"",)), ("readinto", (bytearray(1),))):
    try: print(n, getattr(io.RawIOBase, n)(s, *a))
    except Exception as e: print(n, type(e).__name__, e)
```
CPython 3.14t:
```
_checkClosed TypeError S._checkClosed() takes no arguments (1 given)
_checkSeekable TypeError S._checkSeekable() takes no arguments (1 given)
_checkReadable TypeError S._checkReadable() takes no arguments (1 given)
_checkWritable TypeError S._checkWritable() takes no arguments (1 given)
write NotImplementedError 
readinto NotImplementedError 
```
Lucifer:
```
_checkClosed None
_checkSeekable UnsupportedOperation m
_checkReadable UnsupportedOperation m
_checkWritable UnsupportedOperation m
write UnsupportedOperation write
readinto UnsupportedOperation readinto
```

C _io: _checkClosed() etc. take no arguments; RawIOBase.write and readinto raise NotImplementedError.


