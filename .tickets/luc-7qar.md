---
id: luc-7qar
status: open
deps: []
links: []
created: 2026-11-07T14:32:49Z
type: bug
priority: 3
---
# Frames: a finalizer run by a function's return sees the returning frame

def g(): p = P(); return 1 — P.__del__'s sys._getframe(1) is the caller in CPython 3.14 (the frame is popped before its locals are cleared) but g in lucifer, with p bound in the interpreter and every local unbound in compiled code (the return's releases publish no state: the frames guard needs a block that continues). Pop the frame first in both tiers.

