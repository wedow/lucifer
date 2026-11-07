---
id: luc-8h3o
status: closed
deps: []
links: []
created: 2026-11-07T08:52:46Z
type: bug
priority: 1
tags: [jit]
---
# jit: exit at DIV_F inside inlined float.__truediv__ corrupts the caller's stack (select.poll under deopt:N)

On master 4528888, tests/diff/corpus/async_cancellation_finally.py under -X lucifer_jit=always -X lucifer_stress=deopt:11 (also :21) fails with TypeError: 'code' object is not iterable in list(self._fds.items()) of lib/modules/select.py poll.poll. With LUCIFER_JIT_ONLY=poll.poll it still fails, and LUCIFER_JIT_DEBUG shows the exit taken is the stress exit of DIV_F at float.__truediv__@7 (depth 2), inlined into poll@87 (timeout / 1000.0 with an int timeout: I2F then DIV_F). Its frame state is [__truediv__@7 ...] [poll@87 v0 v4 | v4 v108]: the caller keeps the BINARY_OP's operands. Ordinary runs never take that exit (the divisor is not zero); stress reaches it only when few guards precede it, which luc-x8q2's guard removal does at deopt:7, so the check-jit deopt lane now fails on the 4 async corpus programs. A plain-module copy of the function does not reproduce; the module is kernel code.

## Acceptance Criteria

The DIV_F exit (and any guard of a folded operator island in an inlined kernel method) resumes with the caller's stack as the interpreter expects; the async corpus passes under deopt:7, :11 and :21; a binary-level test that takes the exit.


## Notes

**2026-11-07**

Cause: binary_op inlines float's operator method (float_binary, and int / int's float.__truediv__) while the operands are still on the builder's stack, so the inlined frame's caller state is after the operator with the operands still there; the method's folded arithmetic (DIV_F) took that state. Fix: folded arithmetic in a method inlined for an operator leaves at the operator itself (the design's "restarts from its operator's boundary"). tests/kernel/jit_float_island.py runs a child under deopt:1..12: master fails at deopt:5 and :10 with TypeError: 'int' object is not callable; fixed with the change.
