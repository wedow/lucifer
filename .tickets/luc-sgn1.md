---
id: luc-sgn1
status: open
deps: []
links: []
created: 2026-11-04T01:38:48Z
type: bug
priority: 2
---
# Inherited SIG_IGN for SIGINT is replaced by default_int_handler


A process started with SIGINT ignored (a background job of a non-interactive shell) keeps it ignored under CPython: `signal.getsignal(SIGINT)` is `SIG_IGN` and no handler is installed. bin/lucifer installs `default_int_handler` anyway. Seen as a builtin_modules kernel-program diff when `make check-kernel` runs in the background. Repro: `(trap '' INT; bin/lucifer -I -S -c "import signal;print(signal.getsignal(2))")` prints the handler; the oracle prints 1.
