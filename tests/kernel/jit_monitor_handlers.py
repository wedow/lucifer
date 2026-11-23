# Tools switched on by a call made from inside a hot loop whose handler
# compiled code takes: sys.monitoring's RAISE and EXCEPTION_HANDLED
# events, then sys.settrace's exception events, must each see every
# raise after the call that switched them on, as the interpreter's
# unwinding reports them.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
mon = sys.monitoring
TOOL = 3
counts = {'raise': 0, 'handled': 0, 'trace': 0}


def on_raise(code, offset, exc):
    if code.co_name == 'hot':
        counts['raise'] += 1


def on_handled(code, offset, exc):
    if code.co_name == 'hot':
        counts['handled'] += 1


def start_monitoring():
    mon.use_tool_id(TOOL, 'test')
    mon.register_callback(TOOL, mon.events.RAISE, on_raise)
    mon.register_callback(TOOL, mon.events.EXCEPTION_HANDLED, on_handled)
    mon.set_events(TOOL, mon.events.RAISE | mon.events.EXCEPTION_HANDLED)


def stop_monitoring():
    mon.set_events(TOOL, 0)
    mon.free_tool_id(TOOL)


def tracer(frame, event, arg):
    if frame.f_code.co_name == 'hot':
        if event == 'exception':
            counts['trace'] += 1
        return tracer
    return None


def start_trace():
    sys.settrace(tracer)
    sys._getframe(1).f_trace = tracer      # the running loop's frame too


def hot(n, switch):
    d = {}
    s = 0
    for j in range(n):
        try:
            s += d[j]
        except KeyError:
            s += 1
        if j == n // 2:
            switch()
    return s


print(hot(N, start_monitoring), counts)
stop_monitoring()
print(hot(N, start_trace), counts)
sys.settrace(None)
print(hot(N, lambda: None), counts)
