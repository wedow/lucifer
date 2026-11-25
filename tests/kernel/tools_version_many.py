# Local events switched on and off 600,000 times on one code, with a hot loop
# running it in line now and then: each change moves the tools version for
# compiled code, and no number of changes runs out (CPython spends nothing on
# local events). The LINE events are the oracle's.
import sys

mon = sys.monitoring
E = mon.events
TOOL = 3
lines = 0


def on_line(code, line):
    global lines
    lines += 1


def f(x):
    return x + 1


def hot(n):
    s = 0
    for i in range(n):
        s += f(i)
    return s


mon.use_tool_id(TOOL, 'many')
mon.register_callback(TOOL, E.LINE, on_line)
total = 0
for i in range(600_000):
    mon.set_local_events(TOOL, f.__code__, E.LINE if i % 2 else 0)
    if i % 50_000 == 1:
        total += hot(2000)
mon.set_local_events(TOOL, f.__code__, 0)
print(total, lines, hot(2000), lines)
mon.free_tool_id(TOOL)
