---
id: luc-g4lr
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Recursion limit: str(), property getters and generator resumes cost more frames than in CPython

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): textlibs_diff fns emailfr.get_comment, plist.*, 15 cases over seeds 1 and 2 at 1,000 cases each.

```python
import sys
def depth():
    f, d = sys._getframe(), 0
    while f: d += 1; f = f.f_back
    return d
class A:
    def __init__(s, k): s.k = k
    def __str__(s): return str(s.k) if isinstance(s.k, A) else "x"
class B:
    def __init__(s, k): s.k = k
    @property
    def content(s): return s.k.content if isinstance(s.k, B) else "x"
class C:
    def __init__(s, k): s.k = k
    def f(s): return "".join(c.f() for c in s.k) if s.k else "x"
def tryit(name, fn):
    res = []
    for extra in range(2, 12):
        sys.setrecursionlimit(depth() + extra)
        try: fn(); res.append("ok")
        except RecursionError: res.append("RE")
        finally: sys.setrecursionlimit(1000)
    print(name, " ".join(res))
a = A(A(A("x"))); b = B(B(B("x"))); c = C([C([C([])])])
tryit("str()", lambda: str(a))
tryit("property", lambda: b.content)
tryit("join-genexpr", lambda: c.f())
```
CPython 3.14t:
```
str() RE ok ok ok ok ok ok ok ok ok
property ok ok ok ok ok ok ok ok ok ok
join-genexpr RE RE RE ok ok ok ok ok ok ok
```
Lucifer:
```
str() RE RE RE RE RE RE RE RE RE RE
property RE RE ok ok ok ok ok ok ok ok
join-genexpr RE RE RE RE RE ok ok ok ok ok
```

```python
import sys
class T:
    def __init__(s, k): s.k = k
    def __str__(s): return "(" + "".join(str(c) for c in s.k) + ")"
    @property
    def content(s): return "".join(c.content for c in s.k)
def depth():
    f, d = sys._getframe(), 0
    while f: d += 1; f = f.f_back
    return d
t = T([T([T([])])])
for extra in range(2, 14):
    sys.setrecursionlimit(depth() + extra)
    try: str(t); a = "ok"
    except RecursionError: a = "RE"
    try: t.content; b = "ok"
    except RecursionError: b = "RE"
    sys.setrecursionlimit(1000)
    print(extra, a, b)
```
CPython 3.14t:
```
2 RE RE
3 RE RE
4 RE RE
5 ok ok
6 ok ok
7 ok ok
8 ok ok
9 ok ok
10 ok ok
11 ok ok
12 ok ok
13 ok ok
```
Lucifer:
```
2 RE RE
3 RE RE
4 RE RE
5 RE RE
6 RE RE
7 RE RE
8 RE ok
9 RE ok
10 RE ok
11 RE ok
12 RE ok
13 RE ok
```


