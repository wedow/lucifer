---
id: luc-3llo
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# code objects lack _co_code_adaptive, so dis.get_instructions(adaptive=True) raises

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_diff fn compact, 66 cases over seeds 1 and 2 at 1,000 cases each (the generator inspects the specialized instruction).

```python
import dis
def f(a, b): return a + b
for _ in range(10): f(1, 2)
try: print([i.opname for i in dis.get_instructions(f, adaptive=True)][:3])
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
['RESUME_CHECK', 'LOAD_FAST_BORROW_LOAD_FAST_BORROW', 'BINARY_OP_ADD_INT']
```
Lucifer:
```
AttributeError 'code' object has no attribute '_co_code_adaptive'
```

Lucifer has no adaptive bytecode; returning co_code would let adaptive disassembly work.


