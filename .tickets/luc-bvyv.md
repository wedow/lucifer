---
id: luc-bvyv
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# str indexing with an int beyond Py_ssize_t says 'string index out of range'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_abstract_diff, 78 cases over seeds 1 and 2 at 1,000 cases each.

```python
def t(f):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
t(lambda: 'ab'[2**64])
t(lambda: 'ab'[-2**63-1])
t(lambda: [1, 2][2**64])
t(lambda: (1,)[2**64])
t(lambda: b'ab'[2**64])
t(lambda: range(3)[2**64])
```
CPython 3.14t:
```
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError range object index out of range
```
Lucifer:
```
IndexError string index out of range
IndexError string index out of range
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
IndexError range object index out of range
```

CPython: IndexError "cannot fit 'int' into an index-sized integer".


