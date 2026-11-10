---
id: luc-eqoy
status: open
deps: []
links: []
created: 2026-11-10T05:15:07Z
type: task
priority: 2
tags: [perf, capi]
---
# telco is 12.4x CPython with _decimal: profile the extension-call boundary

With CPython's _decimal over libmpdec (luc-clibs), pyperformance telco went from 51x to 12.4x CPython 3.14t (best of 3: 0.0105 s CPython, 0.538 s master, 0.131 s branch). The pure-Python diagnosis expected about 3.65x of tier overhead once libmpdec does the arithmetic. A short perf record of the worker puts libmpdec and _decimal at a small share and Lucifer at 85% (eval_frame 21%, dict probes, type_lookup, allocation), but the sample is small and includes startup (unverified). Profile a longer run: Decimal operators reach nb_add/nb_multiply through the kernel's generic binary_op and wrapper descriptors, and Decimal construction through type.__call__, so the extension-call path is the likely cost.

