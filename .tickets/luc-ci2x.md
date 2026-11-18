---
id: luc-ci2x
status: open
deps: []
links: []
created: 2026-11-18T21:06:18Z
type: feature
priority: 2
tags: [jit, perf]
---
# jit: direct entries take calls that leave parameters to their defaults

luc-tgqa's direct entries take a call only when it passes exactly the code's positional parameters. A call that leaves trailing parameters to the function's defaults (def f(a, b, c=7)) still goes h_call -> call_function -> lu_frame_push -> lu_eval -> lu_jit_run. The call record now says how many arguments the caller passes; the entry declines when the defaults cannot fill the rest, and binds the defaults' last items to the parameters past the arguments (the tuple read once).

## Acceptance Criteria

tests/kernel/jit_compiled_calls.py's defaulted callee is entered directly (direct calls flat in N); rows with defaulted callees lose instructions on a core lease; gates pass.


## Notes

2026-11-18, not landed. Fires (Richards +592 k compiled entries, pickle_pure_python 387 k C entries made
compiled, Havlak 50 k) but instructions flat: DeltaBlue 0.987, others +0.2 to +0.4% from the count the caller
stores and the entry compares on every call. A version that costs the common call nothing: a second entry for
fewer arguments, published beside the first, taken only when the caller's count is short.
