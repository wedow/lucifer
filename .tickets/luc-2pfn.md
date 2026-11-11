---
id: luc-2pfn
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# str(int) enforces the digit limit exactly where CPython's size pre-check lets slightly longer ints through

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_str_diff fns str, fmt, intstr, 5 cases over seeds 1 and 2 at 1,000 cases each.

```python
import sys
for lim, n in ((9000, 9032), (9000, 9001), (9000, 9020), (4300, 4301), (4300, 4400), (4300,4500), (9100, 9206)):
    sys.set_int_max_str_digits(lim)
    v = 10 ** (n - 1)
    for name, f in (("str", str), ("hex-free-fmt", lambda x: format(x, "d")), ("repr", repr)):
        try: print(lim, n, name, len(f(v)))
        except Exception as e: print(lim, n, name, type(e).__name__, str(e)[:60])
```
CPython 3.14t:
```
9000 9032 str 9032
9000 9032 hex-free-fmt 9032
9000 9032 repr 9032
9000 9001 str ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9001 hex-free-fmt ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9001 repr ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 str ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 hex-free-fmt ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 repr ValueError Exceeds the limit (9000 digits) for integer string conversio
4300 4301 str ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4301 hex-free-fmt ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4301 repr ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4400 str ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4400 hex-free-fmt ValueError Exceeds the limit (4300 digits) for integer string conversio
... (7 more lines)
```
Lucifer:
```
9000 9032 str ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9032 hex-free-fmt ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9032 repr ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9001 str ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9001 hex-free-fmt ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9001 repr ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 str ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 hex-free-fmt ValueError Exceeds the limit (9000 digits) for integer string conversio
9000 9020 repr ValueError Exceeds the limit (9000 digits) for integer string conversio
4300 4301 str ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4301 hex-free-fmt ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4301 repr ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4400 str ValueError Exceeds the limit (4300 digits) for integer string conversio
4300 4400 hex-free-fmt ValueError Exceeds the limit (4300 digits) for integer string conversio
... (7 more lines)
```

CPython first compares the digit-array size with an approximation of the limit; matching it means copying that pre-check.


