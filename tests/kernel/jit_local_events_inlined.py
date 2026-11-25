# sys.monitoring.set_local_events switched on for a code that is already hot:
# a callee its hot caller runs in line, and the code of a loop that is running,
# from a callee the loop calls. The LINE events begin at once, as on CPython.
import sys

mon = sys.monitoring
TOOL = 4
E = mon.events
mon.use_tool_id(TOOL, 'local')
seen = []


def on_line(code, line):
    seen.append((code.co_name, line - code.co_firstlineno))


mon.register_callback(TOOL, E.LINE, on_line)


def inc(x):
    y = x + 1
    return y


def caller(n):
    s = 0
    for i in range(n):
        s = inc(s)
    return s


print(caller(3000))
mon.set_local_events(TOOL, inc.__code__, E.LINE)
print(caller(5))
print('inlined callee', len(seen), sorted(set(seen)))
mon.set_local_events(TOOL, inc.__code__, 0)
print(caller(5), len(seen))
seen.clear()

AT = 2500


def arm(i):
    if i == AT:
        mon.set_local_events(TOOL, loop.__code__, E.LINE)
    return i & 1


def loop(n):
    s = 0
    for i in range(n):
        s += arm(i)
        s += 1
    return s


print(loop(AT + 10))
print('running loop', len(seen), sorted(set(seen)))
mon.set_local_events(TOOL, loop.__code__, 0)
seen.clear()


# The same for a loop whose code has never had events, called from a hot
# caller.
def arm2(i):
    if i == AT:
        mon.set_local_events(TOOL, loop2.__code__, E.LINE)
    return i & 1


def loop2(n):
    s = 0
    for i in range(n):
        s += arm2(i)
        s += 1
    return s


def outer(n):
    return loop2(n)


for _ in range(20):
    outer(100)
print(outer(AT + 10))
print('running loop, called', len(seen), sorted(set(seen)))
mon.set_local_events(TOOL, loop2.__code__, 0)
mon.register_callback(TOOL, E.LINE, None)
mon.free_tool_id(TOOL)
