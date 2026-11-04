---
id: luc-bx6c
status: open
deps: []
links: []
created: 2026-11-04T21:17:15Z
type: bug
priority: 2
---
# An uncaught KeyboardInterrupt exits 1, not by SIGINT (-2) as CPython does; threads_signal_sort races into it

tests/kernel/threads_signal_sort.py: when the sorting thread's interrupt_main() lands before the main thread enters its try (a race in the test, more likely now that the key function runs compiled), both the oracle and lucifer end with an uncaught KeyboardInterrupt at t.start(). The oracle then exits by SIGINT (status -2, Modules/main.c exit_sigint); lucifer exits 1, so check-kernel fails about 1 run in 3. Fix the exit status, and make the test wait until the main thread is in its try before signalling.

