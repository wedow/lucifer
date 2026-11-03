---
id: luc-2yau
status: closed
deps: [luc-52bm]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1e: str, f-strings and formatting

Storage primitives for 1/2/4-byte char data (copy, fill, compare, find, count, widen/narrow, UTF-8 encode/decode) and the compact str layout with state.interned. Kernel str in Python: all str methods (split/rsplit/partition/join/replace/strip/find/index/count/startswith/endswith/case maps over Unicode data primitives, isX predicates, expandtabs, zfill, center/ljust/rjust, translate/maketrans, encode for utf-8/latin-1/ascii), repr/ascii escaping, slicing and iteration, comparison, hash via the SipHash primitive, interning. Formatting: the format-spec mini-language (de-RPython'd from PyPy pypy/objspace/std/newformat.py), str.format/format_map, %-formatting (pypy/objspace/std/formatting.py), int.__format__. Opcodes: FORMAT_SIMPLE, FORMAT_WITH_SPEC, CONVERT_VALUE, BUILD_STRING.
Implements: DESIGN 4.1 (str payload, hashes), 4.4 (storage primitives).
Files: runtime/prim/storage.c, runtime/prim/unicodedata.c (generated tables from third_party/cpython), lib/kernel/{str.py,_format.py}.
Adds tests/kernel: str_methods.py, fstrings.py, str_format.py.

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="str_methods fstrings str_format" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).


## Notes

**2026-11-03T05:24:01Z**

closed: str, f-strings, formatting; 277+ kernel programs match the oracle
