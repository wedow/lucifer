---
id: luc-uexr
status: open
deps: []
links: [luc-m3wi, luc-p2yt, luc-9ags, luc-o8i6, luc-j2vs, luc-4yfw, luc-n6yj, luc-6ljp, luc-r1pk, luc-mltz, luc-xtmp, luc-po0x]
created: 2026-11-08T14:06:52Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: kernel __new__ bodies too large to inline whole (str, int, float, range)

After luc-p2yt, str(x), int(s), float(x), range(a, b) call their kernel __new__ directly, but it never inlines: each makes calls an inlined frame cannot (str.__new__ calls _str_new, int.__new__ to_int/_parse, range.__new__ index), so one direct call per construction remains (jit_new_calls.py: 28 compiled calls per iteration of its loop). Split the common case (an exact int, str or float argument) into a small kernel path that inlines, the rest behind a call.


## Notes

**2026-11-08T22:04:06Z**

luc-r1pk tried a small first branch in str.__new__ and int.__new__ (cls is str/int, one positional argument: return to_str/to_int of it): tests/kernel/jit_new_calls.py's compiled calls did not drop (1.227M -> 1.242M at 20000 iterations), since to_str and to_int stay calls; dropped, not committed. Open.
