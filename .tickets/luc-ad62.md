---
id: luc-ad62
status: open
deps: []
links: []
created: 2026-11-21T09:33:25Z
type: bug
priority: 2
tags: [capi, recursion]
---
# capi given_matches_run: the RecursionError past the C stack limit is not reported as unraisable

tests/capi/given_matches_run.py (check-capi-ext) fails on master 53e00da8 and in the 11-21 daily (3c94548c): for a 500,000-deep nested tuple CPython prints 'False ["RecursionError"]', Lucifer 'False []'. PyErr_GivenExceptionMatches stops at the soft limit and returns 0 as it should, but the unraisable hook never records the RecursionError. Unverified guess: PyErr_WriteUnraisable runs sys.unraisablehook (kernel Python) at the deep C stack point, and since 297f55f2 bounds kernel frames by stack room the hook's own call fails there, so the report is lost. Unrelated to the lock commits (62aaa6bf, 7003c94f, 8fb53ff2, ab39382c): no lock on that path. Fix direction: report the unraisable after unwinding (record the error, return 0, report at the outermost PyErr_GivenExceptionMatches) or give the hook call headroom as CPython's recursion_headroom does.

