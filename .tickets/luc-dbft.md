---
id: luc-dbft
status: closed
deps: []
links: []
created: 2026-11-07T23:49:21Z
type: chore
priority: 3
tags: [perf]
---
# census: re-measure the call protocol share with -X perf fixed

The 2026-11-07 census profiled under -X perf, whose trampoline made the direct entry decline (compiled calls 0) and sent h_call through the generic path: the call protocol and Py* shim shares (10.3% and 8.3%) were measured on a call path normal runs do not take. luc-ibwc lets the direct entry run under the trampoline. Re-profile the probe programs (telco, tomli_loads, pprint, dulwich_log, raytrace) and correct the census numbers.


## Notes

**2026-11-08T14:34:27Z**

Redone on the bench host, perf record -F 999 of the probe programs under -X perf (which no longer sends calls the slow way). Call protocol (lu_jit_run/enter/call, lu_frame_*, alloc_frame, release_refs, h_call*, call_function, function_vectorcall, lu_kernel_target, lu_native_call), master: telco 13.8%, Flask in process 11.7%, dulwich_log 10.8%, pprint 10.0%, raytrace 9.4%, tomli_loads 8.7%. Py* shims: 4.4-5.5%, tomli_loads 8.0%. The census's 10.3% and 8.3% were close for the protocol and high for the shims. Per-callee whynot counts (new -X lucifer_jit_stats lines 'jit: whynot KIND N CALLEE') replace the guesses: the misses are calls of kernel wrapper descriptors through call_special (pprint 46M, tomli_loads 41M, raytrace 11M per run), class calls of builtin types (dulwich_log bytes 731K, mako str 539K), and star, keyword-only and cell parameters (luc-p2yt).
