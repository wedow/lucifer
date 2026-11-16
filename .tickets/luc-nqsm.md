---
id: luc-nqsm
status: open
deps: []
links: []
created: 2026-11-16T14:32:30Z
type: bug
priority: 2
---
# concurrent.futures.InterpreterPoolExecutor missing

test_concurrent_futures errors at import (util.py InterpreterPoolMixin reads futures.InterpreterPoolExecutor), so none of its process-pool tests run, forkserver ones included.

