---
id: luc-ahq7
status: closed
deps: [luc-qidc]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1s: frame objects, f_locals write-through and the one frame walker

Lazy PyFrameObject that owns nothing and reads through the frame record, taking a copy on frame exit (take_ownership); FrameLocalsProxy (PEP 667) writing to fast locals and cells; f_back, f_code, f_globals, f_builtins, f_lineno (line table decode, settable only under tracing later), f_lasti, f_trace placeholders, frame.clear() rules for running and suspended frames, gi_frame/cr_frame. One lu_frame_walk(task, visit) used by sys._getframe, traceback creation, PyThreadState_GetFrame, PyFrame_GetBack and the GC's deferred-root scan; it skips kernel-hidden frames for user-visible consumers. Links are single-store pushes/pops so a signal-time reader sees a valid chain. instr is valid at every safepoint and C call.
Implements: DESIGN 5.2 (frames), 6.1 (lazy PyFrameObject); FOUNDATIONS 2.2.
Files: runtime/interp/frame.c (walker, frame object), lib/kernel/frame.py (FrameLocalsProxy, frame/traceback attribute surface).
Adds tests/kernel: frames.py (_getframe(1).f_locals write-through visible to the caller, f_lineno and f_lasti after a call, f_back skipping kernel builtins through sorted(key=...), gi_frame, frame.clear(), tb_frame/tb_lineno).

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="frames" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); the handoff lanes for it are checked in K1u.


## Notes

**2026-11-03T08:32:40Z**

frame objects, FrameLocalsProxy, lu_frame_walk/lu_frame_user and take-ownership landed; tests/kernel/frames.py matches the oracle
