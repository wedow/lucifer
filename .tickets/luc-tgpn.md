---
id: luc-tgpn
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# _operator differs from the C operator module: concat, indexOf/countOf, setitem/delitem, imul, length_hint, compare_digest

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fns operator.*, 22 cases over seeds 1 and 2 at 1,000 cases each.

```python
import operator
for a, b in [({}, 1), ([], 1), ((), [])]:
    try: print(operator.concat(a, b))
    except Exception as e: print(type(e).__name__, e)
class G:
    def __getattr__(self, n): raise ValueError("getattr")
try: operator.concat(G(), G())
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError 'dict' object can't be concatenated
TypeError can only concatenate list (not "int") to list
TypeError can only concatenate tuple (not "list") to tuple
TypeError 'G' object can't be concatenated
```
Lucifer:
```
TypeError unsupported operand type(s) for +: 'dict' and 'int'
TypeError can only concatenate list (not "int") to list
TypeError can only concatenate tuple (not "list") to tuple
ValueError getattr
```

```python
import operator
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
t(operator.indexOf, 1, 3)
t(operator.countOf, 1, 3)
t(operator.delitem, __import__("collections").deque([1]), "s")
t(operator.setitem, (1,), 2**63, 1)
class P: pass
t(operator.imul, P(), (1,))
t(operator.truth, type("T", (), {"__bool__": 5})())
t(operator.length_hint, type("H", (), {"__length_hint__": property(lambda s: (_ for _ in ()).throw(TypeError("get")))})(), 7)
```
CPython 3.14t:
```
TypeError argument of type 'int' is not iterable
TypeError argument of type 'int' is not iterable
TypeError sequence index must be integer, not 'str'
IndexError cannot fit 'int' into an index-sized integer
TypeError unsupported operand type(s) for *=: 'P' and 'tuple'
TypeError 'int' object is not callable
TypeError get
```
Lucifer:
```
TypeError 'int' object is not iterable
TypeError 'int' object is not iterable
TypeError 'str' object cannot be interpreted as an integer
TypeError 'tuple' object does not support item assignment
TypeError can't multiply sequence by non-int of type 'P'
TypeError 'int' object is not callable
7
```

```python
import hmac
class S(str): pass
for a, b in [(S("a"), b"a"), (memoryview(bytearray(4))[::2], b"ab")]:
    try: print(hmac.compare_digest(a, b))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError a bytes-like object is required, not 'S'
BufferError memoryview: underlying buffer is not C-contiguous
```
Lucifer:
```
TypeError a bytes-like object is required, not 'str'
False
```


