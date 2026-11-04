---
id: luc-3s4d
status: closed
deps: []
links: []
created: 2026-11-04T16:14:00Z
type: bug
priority: 1
assignee: Greg Wedow
---
# signals_deep_recursion: segfault when the interrupt never arrives

tests/kernel/signals_deep_recursion.py (from luc-wm18) fails make check under host load: the oracle prints 'interrupt lost' and lucifer segfaults. A master build also segfaults when the interrupt never arrives, so the crash path is real: lucifer must hit RecursionError, not overflow the 512 KiB stack, on the same recursion the oracle survives. Fix the crash; make the test deterministic so it does not depend on signal timing under load (e.g. raise_signal from inside the recursion rather than a timer). Found during luc-sx8l.

