---
id: luc-prb3
status: closed
deps: []
links: []
created: 2026-11-04T08:00:59Z
type: bug
priority: 3
assignee: Greg Wedow
---
# set SIGPIPE and SIGXFSZ to SIG_IGN at runtime init

CPython's signal_install_handlers ignores SIGPIPE and SIGXFSZ at init; lucifer leaves them at SIG_DFL, so getsignal(SIGPIPE) is 1 on the oracle and 0 here, and a write to a closed pipe kills the process instead of raising BrokenPipeError. Found by GLM during luc-sgn1.


## Notes

**2026-11-04T09:32:32Z**

Fixed on branch luc-c702 (SIGPIPE ignored at startup); close when c702 merges. Check SIGXFSZ too.

**2026-11-04T11:33:22Z**

Fixed by luc-c702 (pylifecycle ignores SIGPIPE and SIGXFSZ; exec restores them).
