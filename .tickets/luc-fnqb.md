---
id: luc-fnqb
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: test_os worker returns empty JSON after 50 cases (248 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. regrtest reports 'worker bug (empty JSON)' for test_os: the worker exits without writing its result, so 248 cases CPython passes count as errors. Something in test_os (an fd-closing or os._exit path is the likely shape, unverified) breaks the worker's result channel. Reproduce: bin/lucifer -m test -v test_os.

