---
id: luc-020h
status: open
deps: []
links: []
created: 2026-11-24T02:15:26Z
type: bug
priority: 2
tags: [jit]
---
# jit: a class gaining __del__ inside an inlined __init__ while the instance is virtual

Review finding on luc-eud7: code inside an inlined __init__ that assigns __del__ to the class while the instance is still virtual drops the instance without finalizing it.

Two shapes tried on origin/master 8c183450 match CPython in the jit and jit-deopt lanes (the finalizer runs for the instance being made and for the one before it):

```python
log = []
def fin(self): log.append(self.i)
def arm(cls): setattr(cls, '__del__', fin)
class C:
    def __init__(self, i):
        if i == 1500:
            arm(C)                    # or C.__del__ = fin directly
        self.i = i
def run(n):
    s = 0
    for i in range(n):
        c = C(i)
        s += c.i
    return s
print(run(3000), len(log), log[:3])   # CPython: 4498500 1501 [1499, 1500, 1501]
```

Next: the reviewer's own construction, which these do not reach (an instance kept virtual across the assignment with no exit or materialization between the store to the class and the instance's death).

