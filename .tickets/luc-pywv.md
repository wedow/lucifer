---
id: luc-pywv
status: in_progress
deps: []
links: []
created: 2026-11-06T07:34:17Z
type: feature
priority: 1
---
# web benchmark: flask todo app, vertical scaling; drop JIT-off AWFY timing from daily

A: daily times AWFY only JIT on vs the CPython reference (off-mode correctness covered by the kernel lane). B: a Flask Todo app under tools/bench/web served by one threaded WSGI server, measured with wrk at 1,2,4.. server CPUs on CPython 3.14, 3.14t and Lucifer; req/s, p50, p99, scaling efficiency; Web section in the daily summary with CPython numbers in the reference file.


## Notes

**2026-11-06T10:58:30Z**

Branch luc-web: 52e7db7 (no JIT-off AWFY), d2531db (network sockets), 299a545 (web bench + daily wiring). Found luc-c69o (JIT+threads crash), luc-pcs5 (_sre/json perf), luc-ik2m (thread-exit purge churn). Daily needs WEB_SITE (3.14t site-packages with Flask); --refresh-reference also needs WEB_GIL_PYTHON.
