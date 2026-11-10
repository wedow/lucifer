---
id: luc-8uu4
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: pidfd support missing (43 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_asyncio.test_subprocess 41 ('operating system does not support pidfds'), test_asyncio.test_unix_events 1 and test_signal 1 skip: os.pidfd_open and signal.pidfd_send_signal are missing.

