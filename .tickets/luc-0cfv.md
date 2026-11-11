---
id: luc-0cfv
status: open
deps: []
links: []
created: 2026-11-11T03:34:56Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# str/bytes methods do not range-check integer arguments against Py_ssize_t / C int

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): strsearch_diff, all 148 mismatches over seeds 1 and 2 at 1,000 cases each (rjust, ljust, center, zfill, split, rsplit, replace, expandtabs).

```python
# arguments that are Py_ssize_t / C int in CPython are not range-checked
def t(label, f):
    try:
        print(label, repr(f()))
    except Exception as e:
        print(label, type(e).__name__, e)
t("rjust -2**63-1", lambda: "x".rjust(-2**63 - 1))
t("zfill -2**63-1", lambda: b"ab".zfill(-2**63 - 1))
t("center -2**63-1", lambda: "ab".center(-2**63 - 1))
t("split 2**63", lambda: "".split(None, 2**63))
t("rsplit 2**64", lambda: b"".rsplit(None, 2**64))
t("replace count 2**63", lambda: b"".replace(b"\xff", b"\x00\xff", 2**63))
t("split empty sep, 2**63", lambda: "".split("", 2**63))
t("expandtabs -2**31-1", lambda: b"\r".expandtabs(-2**31 - 1))
t("expandtabs 2**63-1", lambda: "a\t".expandtabs(2**63 - 1))
t("ljust 2**63 str", lambda: "".ljust(2**63))
t("ljust 2**63 bytes", lambda: b"".ljust(2**63))
t("center 2**63 bytearray", lambda: bytearray(b"a").center(2**63))
```
CPython 3.14t:
```
rjust -2**63-1 OverflowError Python int too large to convert to C ssize_t
zfill -2**63-1 OverflowError Python int too large to convert to C ssize_t
center -2**63-1 OverflowError Python int too large to convert to C ssize_t
split 2**63 OverflowError Python int too large to convert to C ssize_t
rsplit 2**64 OverflowError Python int too large to convert to C ssize_t
replace count 2**63 OverflowError Python int too large to convert to C ssize_t
split empty sep, 2**63 OverflowError Python int too large to convert to C ssize_t
expandtabs -2**31-1 OverflowError Python int too large to convert to C int
expandtabs 2**63-1 OverflowError Python int too large to convert to C int
ljust 2**63 str OverflowError Python int too large to convert to C ssize_t
ljust 2**63 bytes OverflowError Python int too large to convert to C ssize_t
center 2**63 bytearray OverflowError Python int too large to convert to C ssize_t
```
Lucifer:
```
rjust -2**63-1 'x'
zfill -2**63-1 b'ab'
center -2**63-1 'ab'
split 2**63 []
rsplit 2**64 []
replace count 2**63 b''
split empty sep, 2**63 ValueError empty separator
expandtabs -2**31-1 b'\r'
```

lib/kernel/str.py _ssize returns any int unchecked. 'a\t'.expandtabs(2**63-1) hangs or dies where CPython raises OverflowError.


