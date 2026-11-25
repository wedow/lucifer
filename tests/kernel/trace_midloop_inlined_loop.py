# sys.monitoring LINE events switched on from another thread while the
# worker spins in a loop of a callee its hot caller runs in line: the loop's
# lines are reported from then on, as on CPython.
import sys
import threading
import time

mon = sys.monitoring
TOOL = mon.DEBUGGER_ID
E = mon.events
started = threading.Event()
stop = threading.Event()
lines = []


def step(n):
    return (n + 1) & 0xFFFF


def spin(go):
    n = 0
    while go():
        n = step(n)
    return n


def outer(k, go):
    s = 0
    for _ in range(k):
        s += spin(go)
    return s


budget = [0]


def countdown():
    budget[0] -= 1
    return budget[0] > 0


for _ in range(200):
    budget[0] = 20
    outer(3, countdown)


def forever():
    if not started.is_set():
        started.set()
    return not stop.is_set()


def on_line(code, line):
    if code is spin.__code__:
        lines.append(line - code.co_firstlineno)


t = threading.Thread(target=outer, args=(1, forever))
t.start()
started.wait()
time.sleep(0.05)
mon.use_tool_id(TOOL, 'inlined loop')
mon.register_callback(TOOL, E.LINE, on_line)
mon.set_events(TOOL, E.LINE)
while len(lines) < 100:
    time.sleep(0.001)
mon.set_events(TOOL, 0)
mon.free_tool_id(TOOL)
stop.set()
t.join()
print('lines of the running loop', len(lines) >= 100, sorted(set(lines)))
