# A tool switched on, off, on again and off again around a hot loop: each time
# on, the loop's callee reports the same events as on CPython; each time off,
# none, and the results are the same throughout. sys.settrace, sys.setprofile,
# sys.monitoring global events, and local events on the callee's code.
import sys

mon = sys.monitoring
E = mon.events
TOOL = 3
log = {}


def rec(*key):
    log[key] = log.get(key, 0) + 1


def leaf(x):
    y = x * 3
    return y - 1


def loop(n):
    s = 0
    for i in range(n):
        s += leaf(i)
    return s


def tracer(frame, event, arg):
    if frame.f_code is leaf.__code__:
        rec('trace', event, frame.f_lineno - leaf.__code__.co_firstlineno)
        return tracer
    return None


mon.use_tool_id(TOOL, 'again')
mon.register_callback(TOOL, E.PY_START, lambda code, off: code is leaf.__code__ and rec('start'))
mon.register_callback(TOOL, E.LINE, lambda code, line: code is leaf.__code__ and rec('line', line - code.co_firstlineno))
mon.register_callback(TOOL, E.PY_RETURN, lambda code, off, v: code is leaf.__code__ and rec('return'))
EVENTS = E.PY_START | E.LINE | E.PY_RETURN


def switch(tool, on):
    if tool == 'settrace':
        sys.settrace(tracer if on else None)
    elif tool == 'setprofile':
        sys.setprofile(tracer if on else None)
    elif tool == 'monitoring':
        mon.set_events(TOOL, EVENTS if on else 0)
    else:
        mon.set_local_events(TOOL, leaf.__code__, EVENTS if on else 0)


for _ in range(5):
    loop(1000)
for tool in ('settrace', 'setprofile', 'monitoring', 'local'):
    for step, on in (('on', True), ('off', False), ('on again', True), ('off again', False)):
        switch(tool, on)
        result = loop(50)
        switch(tool, False)
        print(tool, step, result, sorted(log.items()))
        log.clear()
        print(tool, step, 'then hot', loop(3000), log)
mon.free_tool_id(TOOL)
