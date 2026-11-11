---
id: luc-ceun
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Sequence iterators, reversed and enumerate cannot be pickled (__reduce__/__setstate__ missing)

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_frame_diff fns iter_seq (about 317 cases), enum_counts (about 289) over seeds 1 and 2 at 1,000 cases each.

```python
class S:
    def __len__(self): return 3
    def __getitem__(self, i):
        if i > 2: raise IndexError
        return i
for it in (iter(S()), reversed(S())):
    for op in ("__reduce__()[2]", "__setstate__(1)"):
        try: print(type(it).__name__, op, eval("it." + op))
        except Exception as e: print(type(it).__name__, op, type(e).__name__, e)
```
CPython 3.14t:
```
iterator __reduce__()[2] 0
iterator __setstate__(1) None
reversed __reduce__()[2] 2
reversed __setstate__(1) None
```
Lucifer:
```
iterator __reduce__()[2] TypeError cannot pickle 'iterator' object
iterator __setstate__(1) AttributeError 'iterator' object has no attribute '__setstate__'
reversed __reduce__()[2] TypeError cannot pickle 'reversed' object
reversed __setstate__(1) AttributeError 'reversed' object has no attribute '__setstate__'
```

```python
e = enumerate(range(100), 2**63)
next(e)
try: print(e.__reduce__()[1][1])
except Exception as ex: print(type(ex).__name__, ex)
```
CPython 3.14t:
```
9223372036854775809
```
Lucifer:
```
TypeError cannot pickle 'enumerate' object
```



## Notes

**2026-11-11T04:01:07Z**

range_iterator has the same gap: rangeslice_diff fns iter, iterfor, reversed, setstate, setstate_obj, 175 of 184 mismatches over seeds 1 and 2 at 500 cases each.

```python
for it in (iter(range(0, 4)), reversed(range(0, 4))):
    try:
        print(type(it).__name__, it.__reduce__()[1][0])
    except Exception as e: print(type(e).__name__, e)
    try:
        it.__setstate__(1); print(list(it))
    except Exception as e: print(type(e).__name__, e)
```
