---
id: luc-krum
status: closed
deps: []
links: []
created: 2026-11-03T01:42:38Z
type: chore
priority: 3
assignee: Greg Wedow
tags: [validation]
---
# ftcorpus: classify 128+N shell-wrapper exits as CRASH; add the parallel check to the ft-cpython lane

## Acceptance Criteria

fake shell-wrapper crash scores CRASH in ftcorpus; ft-cpython parallel-wrapped cases run serially score FAIL


## Notes

**2026-11-03T02:05:46Z**

ftcorpus execute maps 128+N fatal-signal exits to -N (CRASH); ft-cpython parallel lane shares fail_serial_passes with the package lane. Checks added in tools/ftcorpus/check.py; make check-harness passes.
