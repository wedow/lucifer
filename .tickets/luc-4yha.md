---
id: luc-4yha
status: open
deps: []
links: []
created: 2026-11-21T09:13:04Z
type: bug
priority: 0
assignee: Greg Wedow
---
# status: corpus timing holds one exclusive bench lease for ~51 minutes

tools/status runs each timed step under TIMING_PREFIX (the daily sets an exclusive host lease). corpus_timings runs the whole tools/bench/corpus time sweep (113 programs, plus the reference) inside ONE prefixed step: on 11-21 that lease held the whole host 07:53-08:45 UTC and stalled every other project's work. The host rule: a timing lease covers one measurement, a few minutes at most; a sweep loops over short leases so other work interleaves. Make every TIMING_PREFIX step cover one benchmark (one corpus program, one AWFY benchmark, one web CPU point) or a small group under ~5 minutes, merging the per-step JSON exactly as today, so results and summary.csv are unchanged. Keep timings comparable: the same best-of/iteration counts per program. Test in tools/ (make check-harness): a fake TIMING_PREFIX that logs each invocation sees one invocation per program.

