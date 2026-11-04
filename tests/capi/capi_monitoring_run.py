from capi_monitoring import fire
import sys
mon = sys.monitoring
E = mon.events
log = []
mon.use_tool_id(3, 'capi')
def f(): pass
fcode = f.__code__
assert fire(0, f.__code__, 4, 0) == 0 and log == []
mon.register_callback(3, E.PY_START, lambda code, off: code is fcode and log.append(('start', off)))
mon.register_callback(3, E.CALL, lambda code, off, c, a: code is fcode and log.append(('call', off, c, a)))
mon.register_callback(3, E.LINE, lambda code, line: code is fcode and log.append(('line', line)))
mon.set_events(3, E.PY_START | E.CALL | E.LINE)
assert fire(0, f.__code__, 4, 0) == 1
assert fire(1, f.__code__, 6, 42) == 1
assert fire(2, f.__code__, 8, 7) == 1
assert log == [('start', 4), ('call', 6, 42, None), ('line', 7)], log
log.clear()
assert fire(2, fcode, 8, 100000) == 1     # a line no small int caches: the callback keeps it
junk = [i * 7 + 100001 for i in range(1000)]
assert log == [('line', 100000)], log
mon.set_events(3, E.CALL)
assert fire(0, f.__code__, 4, 0) == 0 and fire(1, f.__code__, 6, 1) == 1
def bad(code, off):
    if code is fcode:
        raise KeyError(off)
mon.register_callback(3, E.PY_START, bad)
mon.set_events(3, E.PY_START)
try:
    fire(0, f.__code__, 2, 0)
except KeyError as e:
    assert e.args == (2,)
else:
    raise AssertionError('callback error lost')
mon.register_callback(3, E.PY_START, lambda code, off: mon.DISABLE if code is fcode else None)
assert fire(0, fcode, 2, 0) == 1        # DISABLE turns the tool off in this scope until the version moves
assert fire(0, fcode, 2, 0) == 0
mon.set_events(3, 0)
mon.free_tool_id(3)
print('capi monitoring ok')
