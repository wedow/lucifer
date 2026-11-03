from capi_monitoring import fire
import sys
mon = sys.monitoring
E = mon.events
log = []
mon.use_tool_id(3, 'capi')
def f(): pass
assert fire(0, f.__code__, 4, 0) == 0 and log == []
mon.register_callback(3, E.PY_START, lambda code, off: log.append(('start', off)))
mon.register_callback(3, E.CALL, lambda code, off, c, a: log.append(('call', off, c, a)))
mon.register_callback(3, E.LINE, lambda code, line: log.append(('line', line)))
mon.set_events(3, E.PY_START | E.CALL | E.LINE)
assert fire(0, f.__code__, 4, 0) == 1
assert fire(1, f.__code__, 6, 42) == 1
assert fire(2, f.__code__, 8, 7) == 1
assert log == [('start', 4), ('call', 6, 42, None), ('line', 7)], log
mon.set_events(3, E.CALL)
assert fire(0, f.__code__, 4, 0) == 0 and fire(1, f.__code__, 6, 1) == 1
def bad(code, off): raise KeyError(off)
mon.register_callback(3, E.PY_START, bad)
mon.set_events(3, E.PY_START)
try:
    fire(0, f.__code__, 2, 0)
except KeyError as e:
    assert e.args == (2,)
else:
    raise AssertionError('callback error lost')
mon.register_callback(3, E.PY_START, lambda code, off: mon.DISABLE)
try:
    fire(0, f.__code__, 2, 0)
except ValueError:
    pass
else:
    raise AssertionError('DISABLE of a C event allowed')
mon.set_events(3, 0)
mon.free_tool_id(3)
print('capi monitoring ok')
