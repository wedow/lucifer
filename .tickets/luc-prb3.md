---
id: luc-prb3
status: open
deps: []
links: []
created: 2026-11-04T08:00:59Z
type: bug
priority: 3
assignee: Greg Wedow
---
# set SIGPIPE and SIGXFSZ to SIG_IGN at runtime init

CPython's signal_install_handlers ignores SIGPIPE and SIGXFSZ at init; lucifer leaves them at SIG_DFL, so getsignal(SIGPIPE) is 1 on the oracle and 0 here, and a write to a closed pipe kills the process instead of raising BrokenPipeError. Found by GLM during luc-sgn1.

