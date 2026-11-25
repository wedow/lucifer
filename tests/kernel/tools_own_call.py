# A tool turned on by a call the running frame makes itself: the frame's own
# events after that call (its lines, its calls, its return) begin at once, as
# on CPython. The frame runs compiled: as a root entered at its loop, and as a
# callee its hot caller runs in line; the tools are sys.monitoring events and
# sys.setprofile, each turned on once before so that the code has monitoring
# data and runs compiled again.
import sys

mon = sys.monitoring
E = mon.events
TOOL = 2
EVENTS = E.LINE | E.PY_RETURN | E.CALL
log = []


def rec(*key):
    log.append(key)


def watched(code):
    return code.co_name.startswith('g_')


mon.use_tool_id(TOOL, 'own call')
mon.register_callback(TOOL, E.LINE, lambda code, line: watched(code) and rec('line', code.co_name, line - code.co_firstlineno))
mon.register_callback(TOOL, E.PY_RETURN, lambda code, off, v: watched(code) and rec('return', code.co_name))
mon.register_callback(TOOL, E.CALL, lambda code, off, obj, arg: watched(code) and rec('call', code.co_name, getattr(obj, '__name__', '?')))


def profiler(frame, event, arg):
    if watched(frame.f_code):
        rec('profile', event, frame.f_code.co_name)


PROFILERS = (None, profiler)     # chosen without a branch: compiled code runs the call either way


def switch(tool, on):
    if tool == 'none':
        return
    if tool == 'monitoring':
        mon.set_events(TOOL, EVENTS if on else 0)
    else:
        sys.setprofile(profiler if on else None)


def leaf(x):
    return x + 1


def g_root(tool, n):
    s = 0
    for i in range(n):
        s += leaf(i)
    switch(tool, True)
    s += leaf(s)
    s = s * 2
    switch(tool, False)
    return s


def g_inlined_monitoring(i, at):
    mon.set_events(TOOL, EVENTS * (i == at))
    i = leaf(i)
    mon.set_events(TOOL, 0)
    return i


def g_inlined_setprofile(i, at):
    sys.setprofile(PROFILERS[i == at])
    i = leaf(i)
    sys.setprofile(None)
    return i


def caller(callee, n, at):
    s = 0
    for i in range(n):
        s += callee(i, at)
    return s


for tool in ('monitoring', 'setprofile'):
    switch(tool, True)
    switch(tool, False)
    for _ in range(3):
        g_root('none', 3)
    print(tool, 'root', g_root(tool, 3000), log)
    log.clear()
    callee = globals()['g_inlined_' + tool]
    print(tool, 'inlined', caller(callee, 3000, -1), caller(callee, 3000, 2500), log)
    log.clear()
mon.free_tool_id(TOOL)
