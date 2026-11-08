---
id: luc-uexr
status: open
deps: []
links: [luc-m3wi, luc-p2yt, luc-9ags, luc-o8i6]
created: 2026-11-08T14:06:52Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: kernel __new__ bodies too large to inline whole (str, int, float, range)

After luc-p2yt, str(x), int(s), float(x), range(a, b) call their kernel __new__ directly, but it never inlines: each makes calls an inlined frame cannot (str.__new__ calls _str_new, int.__new__ to_int/_parse, range.__new__ index), so one direct call per construction remains (jit_new_calls.py: 28 compiled calls per iteration of its loop). Split the common case (an exact int, str or float argument) into a small kernel path that inlines, the rest behind a call.

