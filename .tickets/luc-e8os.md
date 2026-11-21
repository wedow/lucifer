---
id: luc-e8os
status: closed
deps: []
links: []
created: 2026-11-21T16:42:42Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# str from UTF-8 decodes byte at a time: no ASCII fast path

luc-tax0 row 3: lu_str_from_utf8 is 20% of unpickle steady cycles, decoding a byte at a time. Add a word-at-a-time ASCII prefix scan (8 bytes per step, high-bit mask), falling back to the existing decoder at the first non-ASCII byte. General: every bytes.decode('utf-8'), file read and str construction from UTF-8 benefits. Check the other UTF-8 entry points share it. Measure unpickle, json_loads and one text-reading row.


## Notes

**2026-11-21T16:58:58Z**

UTF-8 string construction and the codec primitive used separate decoders. The native decoder misreported the reason or byte span for several malformed sequences. Share validation and scan the ASCII prefix eight bytes at a time. Copy all-ASCII input directly into the compact string and start mixed-input validation at the first non-ASCII byte. This covers native construction, marshal, unpickling, bytes.decode, str construction, and incremental text decoding.

The new kernel check failed on the base. It passed with the fix alongside 12 nearby checks in each of the plain, JIT, and JIT-deopt lanes. The ABI audit passed.

**2026-11-21T18:50:44Z**

Median user-space instructions from five runs per build, with a fixed hash seed and both builds made in the same checkout. Counts cover each whole process.

| Workload | Base | Branch | Change |
| --- | ---: | ---: | ---: |
| Decode 1 KiB of ASCII as UTF-8 100,000 times | 4,916,844,376 | 917,777,925 | -81.33% |
| pyperformance unpickle | 7,825,235,645 | 6,831,566,792 | -12.70% |
| pyperformance json_loads | 9,435,244,884 | 9,424,080,100 | -0.12% |
| pyperformance tomli_loads | 68,442,261,690 | 70,154,432,080 | +2.50% |

The JSON row supplies strings. The TOML row reads a mixed UTF-8 file and parses it.

**2026-11-21T23:45:33Z**

Review: the mixed-input validation loop called the exported step per character, two calls each (tomli_loads +3.4% instructions); the loop now calls the inline step and asks for the reason only on error. A differential run against CPython over 1,900 malformed and mixed inputs (every handler, partial and final, bytes.decode, str(), SHORT_BINUNICODE and BINUNICODE, incremental and TextIOWrapper, unaligned views) shows no regression and fixes the pickle error spans and surrogatepass reasons; one divergence predates the branch: an incomplete trailing ED A0 with final=False is an error here and pending in CPython (luc-h1na).

Whole run n=4, PYTHONHASHSEED=0, one core, min of 7, base -> branch: unpickle cycles 4.478G -> 4.111G (-8.2%), instructions 6.959G -> 6.075G (-12.7%); json_loads 5.558G -> 5.654G (noise; spread 5.6-7.4G), 8.695G -> 8.674G (-0.2%); tomli_loads 73.57G -> 71.72G, 126.18G -> 126.37G (+0.2%).
