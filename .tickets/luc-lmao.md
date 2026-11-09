---
id: luc-lmao
status: open
deps: []
links: []
created: 2026-11-09T05:35:26Z
type: bug
priority: 1
---
# libtest: Lib/test cases that hang Lucifer's worker

Cases that hang until regrtest's timeout in the full Lib/test run (2026-11-09), each zeroing its module. Located so far: test_call TestPEP590.test_vectorcall (it hangs once the C-API aborts before it are excluded) (excluded in results/libtest/lucifer.excluded.tsv against this ticket). Module-level timeouts are listed in the triage notes.

