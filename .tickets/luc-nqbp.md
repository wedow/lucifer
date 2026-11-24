---
id: luc-nqbp
status: open
deps: []
links: []
created: 2026-11-24T08:06:50Z
type: task
priority: 2
parent: luc-orn7
tags: [perf, jit]
---
# jit: compiled code keeps running after a walker reads its frames (sys._getframe)

Measured (luc-mopl diagnosis, 2026-11-24): logging takes 113K frame exits per 20 steady values: Logger._log -> findCaller -> sys._getframe makes the inlined frames real, the unit leaves at its next frame check (GUARD_FRAMES, EXIT_FRAME), and the rest of _log runs in the interpreter (Logger._log 3.9% of cycles interpreted, findCaller/makeRecord another 2%), then the next call enters again. Same mechanism wherever a callee inspects its caller's frame (warnings, inspect, logging, traceback formatting).

Task: after a walker made the frames real without writing locals or keeping a frame object that outlives the call, the unit goes on (its virtual state is still valid, or it reloads what the walker could have changed); only a write to locals or a kept frame object forces the exit.

Acceptance: a test program calling sys._getframe(1).f_code.co_name in a hot callee takes no frame exits per iteration (jitcheck growth lane flat in N); logging steady frame exits near zero.

