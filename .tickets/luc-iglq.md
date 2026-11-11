---
id: luc-iglq
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# pickle imports _codecs lazily while decoding UNICODE/STRING opcodes

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pickle_diff fn loads, 5 cases over seeds 1 and 2 at 1,000 cases each: the generator blocks builtins.__import__ during loads.

```python
import builtins, pickle
def no_import(*a, **k):
    raise ImportError("blocked " + a[0])
builtins.__import__ = no_import
for data in (b'V\\u00e9\n.', b'V\xe9', b'V\\uzzzz\n.'):
    for f in (pickle.loads, pickle._loads):
        try:
            print(f.__name__, repr(f(data)))
        except Exception as e:
            print(f.__name__, type(e).__name__, e)
```
CPython 3.14t:
```
loads 'é'
_loads LookupError unknown encoding: raw-unicode-escape
loads UnpicklingError pickle data was truncated
_loads EOFError 
loads UnicodeDecodeError 'rawunicodeescape' codec can't decode bytes in position 0-1: truncated \uXXXX escape
_loads LookupError unknown encoding: raw-unicode-escape
```
Lucifer:
```
loads ImportError blocked _codecs
_loads ImportError blocked _codecs
loads UnpicklingError pickle data was truncated
_loads ImportError blocked _codecs
loads ImportError blocked _codecs
_loads ImportError blocked _codecs
```


