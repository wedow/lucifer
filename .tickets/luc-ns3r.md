---
id: luc-ns3r
status: closed
deps: []
links: []
created: 2026-11-17T14:49:44Z
type: chore
priority: 2
---
# floor: a capi failure or a slow startup probe hides whole corpora

11-17 deopt floor: capi showed 18 MISSING because make check-capi stopped at its first failure (dict_watch_resurrect_run, luc-eih1) before the LD_PRELOAD tests ran; difftest showed 40 NOTRUN as whole first-letter units (m p r s t u w) with no case run, the mark of a failed startup probe, whose limit is 5 s (jit=always startup takes 0.55 s alone, so a loaded nice-10 host can pass 5 s). Alone on master difftest deopt passes all 193.

