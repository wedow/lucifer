---
id: luc-33lu
status: open
deps: []
links: []
created: 2026-11-25T13:32:50Z
type: bug
priority: 2
---
# sys._current_frames() leaves out a thread blocked in Event.wait()

Found by tests/kernel/threads_kill_under_calls.py (luc-o80b stage 0): a worker parked in done.wait() under a chain of calls has no entry in sys._current_frames(); CPython lists its frames (wait, wait, c3 ...). Every lane, jit-off included.

