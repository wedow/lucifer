"""Return events switched on while the returning frames run: sys.monitoring's
PY_RETURN turned on inside a call reports that call's own return and its
callers'; a trace function set on running frames (f_trace) sees their
'return' events; both switched off again mid-run."""
import sys

mon = sys.monitoring
TOOL = 3
mon.use_tool_id(TOOL, 'test')
seen = []
mon.register_callback(TOOL, mon.events.PY_RETURN, lambda code, off, val: seen.append((code.co_name, val)))


def inner(on):
    if on:
        mon.set_events(TOOL, mon.events.PY_RETURN)
    else:
        mon.set_events(TOOL, 0)
    return 'inner'


def middle(on):
    x = inner(on)
    return 'middle'


def outer(on):
    middle(on)
    return 'outer'


outer(True)
print('monitoring on', seen)
seen.clear()
outer(False)
print('monitoring off', seen)
seen.clear()
outer(False)
print('stays off', seen)
mon.free_tool_id(TOOL)

events = []


def tracer(frame, event, arg):
    if event in ('call', 'return'):
        events.append((event, frame.f_code.co_name, arg))
    return tracer


def leaf():
    f = sys._getframe(1)
    while f is not None:
        if f.f_code.co_name in ('mid', 'top'):
            f.f_trace = tracer
        f = f.f_back
    sys.settrace(tracer)
    return 'leaf'


def mid():
    leaf()
    return 'mid'


def top():
    mid()
    return 'top'


top()
sys.settrace(None)
print('trace', events)
events.clear()
top()
sys.settrace(None)
print('trace again', events)
