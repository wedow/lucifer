---
id: luc-yxt9
status: open
deps: []
links: []
created: 2026-11-11T03:55:04Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Building a _testbuffer.ndarray with negative strides exits the process with status 1 and no message

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_abstract_diff fn buffer_contig, about 500 cases over seeds 1 and 2 at 1,000 cases each (the generator builds its cases with _testbuffer, the oracle's module loaded through the C API).

```python
try:
    import _testbuffer as tb
    nd = tb.ndarray([0] * 10, format='B', flags=tb.ND_WRITABLE, shape=[1, 1, 4], strides=[6, 0, -3], offset=9)
    print(nd.nbytes, nd.strides, tb.is_contiguous(nd, 'F'))
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
4 (6, 0, -3) False
```
Lucifer:
```

```

Lucifer prints nothing at all, not even the except branch; the process exits with status 1.



## Notes

**2026-11-11T04:13:47Z**

Under an 8 GiB address-space limit the same constructor raises MemoryError for every list source, even one item; without a limit it outgrows a 24 GiB memory cap. So the ndarray-from-list path asks for an unbounded allocation. bytesobj_diff (about 650 cases, mem.script and the cases after the generator lost sync) has the same cause.

```python
import resource; resource.setrlimit(resource.RLIMIT_AS, (8 << 30, 8 << 30))
import _testbuffer as tb
def t(label, f):
    try:
        a = f()
        print(label, "ok", a.shape, a.format, a.itemsize, flush=True)
    except BaseException as e:
        print(label, type(e).__name__, e, flush=True)
t("one item, default format", lambda: tb.ndarray([1], shape=[1]))
t("one item, B", lambda: tb.ndarray([1], shape=[1], format="B"))
t("bytes source", lambda: tb.ndarray(b"\0\1", shape=[2], format="B"))
t("2 items l", lambda: tb.ndarray([1, 2], shape=[2], format="l"))
t("2x2 d", lambda: tb.ndarray([1.0, 2.0, 3.0, 4.0], shape=[2, 2], format="d"))
```
CPython 3.14t: every line ok. Lucifer:
```
one item, default format MemoryError 
one item, B MemoryError 
bytes source TypeError items must be a list or a tuple
2 items l MemoryError 
2x2 d MemoryError 
```
