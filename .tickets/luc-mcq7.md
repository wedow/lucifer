---
id: luc-mcq7
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: remote debugging (sys.remote_exec, _remote_debugging) unavailable (62 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. test_remote_pdb 45 ('remote debugging is disabled'), test_sys 16 and test_audit 1 ('Remote debugging is not enabled') skip; test_external_inspection and test_asyncio.test_tools need _remote_debugging (counted under the extension modules ticket).

