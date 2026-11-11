---
id: luc-2wuq
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# format() and % with precision 2147483646 or 2147483647 raise MemoryError instead of 'precision too big'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): binary64_diff fn fmt_big, 12 cases over seeds 1 and 2 at 1,000 cases each.

```python
for p in (2147483646, 2147483647, 2147483648):
    try: format(1.5, f".{p}f")
    except Exception as e: print(p, type(e).__name__, e)
    try: ("%." + str(p) + "g") % 1.5
    except Exception as e: print(p, type(e).__name__, e)
```
CPython 3.14t:
```
2147483646 ValueError precision too big
2147483646 ValueError precision too big
2147483647 ValueError precision too big
2147483647 ValueError precision too big
2147483648 ValueError precision too big
2147483648 ValueError precision too big
```
Lucifer:
```
2147483646 MemoryError 
2147483646 MemoryError 
2147483647 MemoryError 
2147483647 MemoryError 
2147483648 ValueError precision too big
2147483648 ValueError precision too big
```


