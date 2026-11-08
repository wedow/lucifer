---
id: luc-dbft
status: open
deps: []
links: []
created: 2026-11-07T23:49:21Z
type: chore
priority: 3
tags: [perf]
---
# census: re-measure the call protocol share with -X perf fixed

The 2026-11-07 census profiled under -X perf, whose trampoline made the direct entry decline (compiled calls 0) and sent h_call through the generic path: the call protocol and Py* shim shares (10.3% and 8.3%) were measured on a call path normal runs do not take. luc-ibwc lets the direct entry run under the trampoline. Re-profile the probe programs (telco, tomli_loads, pprint, dulwich_log, raytrace) and correct the census numbers.

