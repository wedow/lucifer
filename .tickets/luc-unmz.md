---
id: luc-unmz
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# __sizeof__ of BytesIO, StringIO, dicts, sets and itertools objects differs from CPython

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_share_diff (all 2,000 cases end in a sizeof op), dictset_diff (about 290 cases), itertools_diff (55 cases) over seeds 1 and 2 at 1,000 cases each.

```python
import io
print(io.BytesIO().__sizeof__())
print(io.BytesIO(b"abc").__sizeof__())
print(io.StringIO().__sizeof__())
```
CPython 3.14t:
```
88
88
136
```
Lucifer:
```
32
32
32
```

```python
import itertools
print(itertools.combinations([1, 2, 3], 2).__sizeof__(),
      itertools.combinations_with_replacement([1, 2, 3], 2).__sizeof__(),
      itertools.permutations([1, 2, 3, 4], 2).__sizeof__(),
      itertools.product([1], [2], repeat=2).__sizeof__())
```
CPython 3.14t:
```
88 88 128 96
```
Lucifer:
```
40 40 40 40
```

By design to the extent Lucifer object layouts differ; it hides every other difference in pyio_share_diff. Decide whether __sizeof__ should model CPython sizes.


