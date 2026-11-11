---
id: luc-nytz
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# itertools constructors: keyword and huge-argument validation differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): itertools_diff, about 70 cases over seeds 1 and 2 at 1,000 cases each.

```python
import itertools
class Sub(itertools.islice):
    def __init__(self, *args, **kwargs):
        pass
class SubC(itertools.chain):
    def __init__(self, *args, **kwargs):
        pass
for f in (lambda: itertools.islice([], None, None, None, None, None, stop=1),
          lambda: itertools.islice([], stop=1),
          lambda: itertools.islice([], 1, 2, 3, 4, 5),
          lambda: Sub([], None, None, None, stop=1),
          lambda: Sub([], None, None, None, None, None, stop=1),
          lambda: itertools.zip_longest(x=1),
          lambda: itertools.chain([], x=1),
          lambda: SubC([0], x=1)):
    try:
        f()
        print('ok')
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError islice() takes no keyword arguments
TypeError islice() takes no keyword arguments
TypeError islice expected at most 4 arguments, got 6
ok
TypeError islice expected at most 4 arguments, got 6
TypeError zip_longest() got an unexpected keyword argument
TypeError chain() takes no keyword arguments
ok
```
Lucifer:
```
TypeError islice.__new__() got an unexpected keyword argument 'stop'
TypeError islice.__new__() got an unexpected keyword argument 'stop'
TypeError islice expected at most 4 arguments, got 6
TypeError islice.__new__() got an unexpected keyword argument 'stop'
TypeError islice.__new__() got an unexpected keyword argument 'stop'
TypeError zip_longest.__new__() got an unexpected keyword argument 'x'
TypeError chain.__new__() got an unexpected keyword argument 'x'
TypeError chain.__new__() got an unexpected keyword argument 'x'
```

```python
import itertools
big = 2 ** 63
for f in (lambda: itertools.combinations(5, big),
          lambda: itertools.combinations([1], big),
          lambda: itertools.product(5, repeat=big),
          lambda: itertools.repeat(2, 2 ** 64),
          lambda: itertools.tee([], -big - 1),
          lambda: itertools.islice([], 3, -2 ** 64, 2),
          lambda: itertools.combinations([1], big).__sizeof__(),
          lambda: itertools.permutations([1], big),
          lambda: list(itertools.combinations_with_replacement([], 2 ** 50)),
          lambda: list(itertools.permutations([0, -1], 2 ** 63 - 1)),
          lambda: list(itertools.batched([1], 2 ** 50)),
          lambda: itertools.tee([], 2 ** 60)):
    try:
        r = f()
        print('ok', type(r).__name__, flush=True)
    except Exception as e:
        print(type(e).__name__, e, flush=True)
```
CPython 3.14t:
```
OverflowError Python int too large to convert to C ssize_t
OverflowError Python int too large to convert to C ssize_t
OverflowError Python int too large to convert to C ssize_t
OverflowError Python int too large to convert to C ssize_t
OverflowError Python int too large to convert to C ssize_t
ValueError Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.
OverflowError Python int too large to convert to C ssize_t
OverflowError Python int too large to convert to C ssize_t
MemoryError 
MemoryError 
MemoryError 
MemoryError 
```
Lucifer:
```
TypeError 'int' object is not iterable
ok combinations
TypeError 'int' object is not iterable
ok repeat
ValueError n must be >= 0
ValueError Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.
ok int
ok permutations
ok list
ok list
ok list
exit 124
```

CPython converts r/n/repeat with ssize_t overflow checks before anything else; one tee(.., 2**60) case hangs Lucifer.


