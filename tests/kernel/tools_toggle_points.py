# Tools changed at points compiled code is at: clear_tool_id, free_tool_id
# and restart_events part-way through a hot loop; settrace_all_threads;
# a generator suspended at a yield across a toggle; and a tracer that sets
# f_trace_opcodes on the outermost frame of a recursion only, so that opcode
# events go on and off at every call. Events and results are the oracle's.
import sys
import threading

mon = sys.monitoring
E = mon.events
TOOL = 4
log = {}


def rec(*key):
    log[key] = log.get(key, 0) + 1


def leaf(x):
    return x * 3 - 1


def loop(n, at, action):
    s = 0
    for i in range(n):
        s += leaf(i)
        if i == at:
            action()
            s += leaf(s & 7)
    return s


def show(name, value):
    print(name, value, sorted(log.items()))
    log.clear()


def arm():
    mon.use_tool_id(TOOL, 'points')
    mon.register_callback(TOOL, E.PY_START, lambda code, off: code is leaf.__code__ and rec('start'))
    mon.register_callback(TOOL, E.LINE, lambda code, line: code.co_name == 'loop' and rec('line', line - code.co_firstlineno))
    mon.set_events(TOOL, E.PY_START | E.LINE)


for _ in range(3):
    loop(3000, -1, None)
arm()
show('clear', loop(3000, 2990, lambda: mon.clear_tool_id(TOOL)))
mon.free_tool_id(TOOL)
arm()
show('free', loop(3000, 2990, lambda: mon.free_tool_id(TOOL)))

arm()
mon.register_callback(TOOL, E.PY_START, lambda code, off: code is leaf.__code__ and (rec('start'), mon.DISABLE)[1])
show('disabled', loop(3000, 2990, mon.restart_events))
mon.set_events(TOOL, 0)
mon.free_tool_id(TOOL)


def tracer(frame, event, arg):
    if frame.f_code is leaf.__code__:
        rec('trace', event)
        return tracer
    return None


def all_threads(on):
    threading.settrace_all_threads(tracer if on else None)


show('all threads', loop(3000, 2990, lambda: all_threads(True)))
all_threads(False)


def gen(n):
    for i in range(n):
        yield leaf(i)


g = gen(6000)
s = sum(next(g) for _ in range(3000))
sys.setprofile(lambda frame, event, arg: frame.f_code is gen.__code__ and rec('profile', event))
s += next(g) + next(g)
sys.setprofile(None)
show('suspended generator', s + sum(g))


def depth(n):
    return 0 if n == 0 else leaf(n) + depth(n - 1)


def opcode_tracer(frame, event, arg):
    if frame.f_code is depth.__code__:
        frame.f_trace_opcodes = frame.f_back.f_code is not depth.__code__
        rec('trace', event)
        return opcode_tracer
    return None


for _ in range(200):
    depth(20)
sys.settrace(opcode_tracer)
r = sum(depth(20) for _ in range(30))
sys.settrace(None)
show('opcode ping-pong', r)
