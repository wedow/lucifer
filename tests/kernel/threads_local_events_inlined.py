# One thread switches LINE local events on and off on a callee, again and
# again, while another thread's hot loop runs it in line and is compiled
# again after each change. Then, with the events left on, the loop counts
# the callee's lines exactly. Results are the oracle's throughout.
import sys
import threading

mon = sys.monitoring
E = mon.events
TOOL = 3
lines = [0]


def on_line(code, line):
    lines[0] += 1


def callee(x):
    y = x + 1
    return y


def hot(n):
    s = 0
    for i in range(n):
        s += callee(i)
    return s


mon.use_tool_id(TOOL, 'other thread')
mon.register_callback(TOOL, E.LINE, on_line)
stop = threading.Event()


def toggler():
    k = 0
    while not stop.is_set():
        mon.set_local_events(TOOL, callee.__code__, E.LINE if k % 2 else 0)
        k += 1
    mon.set_local_events(TOOL, callee.__code__, 0)


t = threading.Thread(target=toggler)
t.start()
results = {hot(2000) for _ in range(200)}
stop.set()
t.join()
print(results)
mon.set_local_events(TOOL, callee.__code__, E.LINE)
lines[0] = 0
print(hot(2000), lines[0])
mon.set_local_events(TOOL, callee.__code__, 0)
print(hot(2000), lines[0])
mon.free_tool_id(TOOL)
