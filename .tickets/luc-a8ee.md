---
id: luc-a8ee
status: closed
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Unpacking ignores __length_hint__ errors and does not call iter() on the result of __iter__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fn p_unpack and friends, about 25 cases over seeds 1 and 2 at 1,000 cases each.

```python
class W:
    def __init__(self, k): self.k, self.n = k, 0
    def __iter__(self): return self
    def __next__(self):
        self.n += 1
        if self.n > 3: raise StopIteration
        return self.n
    def __length_hint__(self):
        if self.k == 0: raise KeyError('w-lh')
        if self.k == 1: return 'x'
        if self.k == 2: return -1
class V:
    def __init__(self, k): self.k = k
    def __iter__(self): return W(self.k)
for k in (0, 1, 2):
    try:
        *a, b = V(k); print(a)
    except Exception as e: print(type(e).__name__, e)
    try: print(list(V(k)))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
KeyError 'w-lh'
[1, 2, 3]
TypeError __length_hint__ must be an integer, not str
[1, 2, 3]
ValueError __length_hint__() should return >= 0
[1, 2, 3]
```
Lucifer:
```
[1, 2]
[1, 2, 3]
[1, 2]
[1, 2, 3]
[1, 2]
[1, 2, 3]
```

```python
class W:
    def __init__(self, k): self.k = k
    def __iter__(self):
        if self.k == 1: raise ValueError('w-iter')
        return iter([7, 8, 9])
    def __next__(self): raise StopIteration
class V:
    def __init__(self, k): self.k = k
    def __iter__(self): return W(self.k)
for k in (1, 2):
    try:
        *a, b = V(k); print(a)
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError w-iter
[7, 8]
```
Lucifer:
```
ValueError not enough values to unpack (expected at least 1, got 0)
ValueError not enough values to unpack (expected at least 1, got 0)
```



## Notes

**2026-11-21T09:29:25Z**

Starred unpacking consumed the first iterator directly after filling the fixed prefix. CPython 3.14t extends a list from that iterator, which calls iter() again and takes the length hint from the first iterator. Reused the kernel's list-extension helper so the remainder comes from the second iterator and hint errors propagate with their original messages.

The new unpack_starred_protocol check failed on the baseline and passed after the fix. It and the four nearby kernel checks passed in plain, jit, and jit-deopt.
