---
id: luc-d0jb
status: closed
deps: []
links: []
created: 2026-11-03T09:23:30Z
type: bug
priority: 2
tags: [runtime]
---
# time.sleep must resume after a signal handler returns (PEP 475)


## Notes

**2026-11-05T01:39:51Z**

sleep/select/poll/read/write/waitpid/flock/lock timeouts already resumed on master; fixed socket timed waits (shared select deadline loop) and urandom; tests/kernel/eintr_retry.py
