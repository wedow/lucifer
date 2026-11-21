---
id: luc-280o
status: closed
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# PyDict_MergeFromSeq2 message for a non-iterable element differs

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff op c_seq2, 1 case over seeds 1 and 2 at 1,000 cases each.

```python
import _testlimitedcapi as tl
for seq in ([7], [(1, 2, 3)], [1]):
    d = {}
    try:
        tl.dict_mergefromseq2(d, seq, 1)
    except Exception as e:
        print(seq, type(e).__name__, e)
try:
    dict([7])
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
[7] TypeError object is not iterable
[(1, 2, 3)] ValueError dictionary update sequence element #0 has length 3; 2 is required
[1] TypeError object is not iterable
TypeError object is not iterable
```
Lucifer:
```
[7] TypeError cannot convert dictionary update sequence element to a sequence
[(1, 2, 3)] ValueError dictionary update sequence element #0 has length 3; 2 is required
[1] TypeError cannot convert dictionary update sequence element to a sequence
TypeError object is not iterable
```



## Notes

**2026-11-21T06:55:36Z**

CPython 3.14t puts the failing pair index in a TypeError note. PyDict_MergeFromSeq2 used the older text as the exception message and omitted the note. It now uses the same sequence-conversion message as CPython and appends the indexed note through BaseException.add_note. The regression reproduced both differences before the fix and passes with dict_ops, err_dict_update_seq, and exceptions in plain, jit, and jit-deopt. The ABI audit passed.
