---
id: luc-7mw3
status: open
deps: []
links: []
created: 2026-11-11T04:17:20Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Iterator __reduce__ with iter/reversed deleted from builtins raises NameError instead of AttributeError

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytesobj_diff fns list.iter, tuple.iter, 33 cases over seeds 1 and 2 at 1,000 cases each.

```python
import builtins
for mk in (lambda: iter([1]), lambda: reversed([1]), lambda: iter((1,))):
    it = mk()
    name = "reversed" if type(it).__name__ == "list_reverseiterator" else "iter"
    saved = getattr(builtins, name)
    delattr(builtins, name)
    try:
        it.__reduce__()
    except Exception as e:
        print(type(it).__name__, type(e).__name__, e)
    finally:
        setattr(builtins, name, saved)
```
CPython 3.14t:
```
list_iterator AttributeError iter
list_reverseiterator AttributeError reversed
tuple_iterator AttributeError iter
```
Lucifer:
```
list_iterator NameError name 'iter' is not defined
list_reverseiterator NameError name 'reversed' is not defined
tuple_iterator NameError name 'iter' is not defined
```


