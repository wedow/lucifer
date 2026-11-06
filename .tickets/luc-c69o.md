---
id: luc-c69o
status: open
deps: []
links: []
created: 2026-11-06T10:07:51Z
type: bug
priority: 0
tags: [jit, threads, crash]
---
# JIT + threads: a caller's local reads NULL after a nested Python call (SIGSEGV in flask redirect)

Found by the web benchmark (luc-pywv). Symptom: SIGSEGV, Py_INCREF(NULL) in eval_frame LOAD_FAST_LOAD_FAST (ceval.c:1447) in flask.helpers.redirect, whose local 0 (location) is NULL while locals 1-2 are intact. redirect is called from the app's toggle view as redirect(request.referrer or "/", 303) with a valid str; the local is NULL by the time redirect reads it, after TO_BOOL on current_app (a werkzeug LocalProxy, so __bool__ runs Python). Caller frames are JIT code (h_call / lu_jit_enter). Same fault site in 3 cores (2 from the HTTP server at 8 CPUs, 1 in-process).

Reproduction (tools/bench/web/toggle_crash.py on branch luc-web; SITE = a 3.14t site-packages with Flask 3.1):
  PYTHONPATH=SITE taskset -c 0-3,6-9 bin/lucifer -X lucifer_jit=on tools/bench/web/toggle_crash.py 400 8
Rates (400 requests per thread): 8 threads: -X lucifer_jit=on 3/3 crash, =always 3/3 crash; =always -X lucifer_stress=deopt:7 0/3, deopt:3 0/3; =off 0/3. 1 thread, jit=on: 0/3. Also crashes 3/3 at ebc38da (master before the luc-6e6n merge, same fault site), so luc-6e6n (inlined frames making calls) is not the cause, though the kept/borrowed-frame paths are still a suspect. Needs threads, so look at per-thread state the JIT shares: compiled code or caches mutated while another thread runs them, frame/datastack bookkeeping across a call that runs Python, or a deopt from one thread rewriting state another thread's frame uses.

