---
id: luc-6cvl
status: closed
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Exception construction calls a subclass's __setattr__ for args

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 170 cases over seeds 1 and 2 at 1,000 cases each.

```python
class S(Exception):
    def __setattr__(self, n, x):
        print("hook", n)
        object.__setattr__(self, n, x)
S()
print("--")
S(1, 2)
```
CPython 3.14t:
```
--
```
Lucifer:
```
hook args
hook args
--
hook args
hook args
```



## Notes

**2026-11-21T11:28:43Z**

The kernel constructors assigned args through instance attribute lookup. CPython writes the BaseException field directly (Objects/exceptions.c). Use the existing BaseException.args descriptor setter for every constructor write. The regression test covers subclass hooks, shadowed args, and explicit assignment. It failed on the baseline and passed with the fix. The regression and four nearby kernel checks passed in plain, jit, and jit-deopt.
