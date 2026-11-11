---
id: luc-4we6
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# OSError.characters_written always reads 0 and never calls __index__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 130 cases over seeds 1 and 2 at 1,000 cases each.

```python
class Ix:
    def __index__(self):
        print("Ix.__index__")
        return 3
try: print(FileNotFoundError(3, None, -1).characters_written)
except Exception as e: print(type(e).__name__, e)
try: print(OSError(1, 'x').characters_written)
except Exception as e: print(type(e).__name__, e)
try: print(BlockingIOError(1, 'x').characters_written)
except Exception as e: print(type(e).__name__, e)
try: print(BlockingIOError(1, 'x', Ix()).characters_written)
except Exception as e: print(type(e).__name__, e)
try: print(BlockingIOError(1, 'x', 'q').characters_written)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
AttributeError characters_written
AttributeError characters_written
AttributeError characters_written
Ix.__index__
3
AttributeError characters_written
```
Lucifer:
```
0
0
0
0
0
```


