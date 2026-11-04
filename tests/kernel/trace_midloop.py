# settrace and sys.monitoring LINE/BRANCH switched on in the middle of a hot
# loop, from the loop's own thread and from another thread.
import sys
import threading

mon = sys.monitoring
TOOL = mon.DEBUGGER_ID
E = mon.events


def work(i):
    return i + 1


# Same thread: settrace turns on at i == 100 and off at i == 200, then
# sys.monitoring LINE and BRANCH_* turn on at 300 and off at 400.
calls = []


def tracer(frame, event, arg):
    if event == "call":
        calls.append(frame.f_code.co_name)
    return None


lines = []
branches = []


def on_line(code, line):
    if code.co_name == "same":
        lines.append(line)


def on_branch(code, src, dst):
    if code.co_name == "same":
        branches.append(dst > src)


def same():
    total = 0
    for i in range(500):
        if i == 100:
            sys.settrace(tracer)
        if i == 200:
            sys.settrace(None)
        if i == 300:
            mon.use_tool_id(TOOL, "midloop")
            mon.register_callback(TOOL, E.LINE, on_line)
            mon.register_callback(TOOL, E.BRANCH_LEFT, on_branch)
            mon.register_callback(TOOL, E.BRANCH_RIGHT, on_branch)
            mon.set_events(TOOL, E.LINE | E.BRANCH_LEFT | E.BRANCH_RIGHT)
        if i == 400:
            mon.set_events(TOOL, 0)
            mon.free_tool_id(TOOL)
        total += work(i)
    return total


print(same())
print("traced calls", len(calls), set(calls))
print("lines", len(lines), sorted(set(lines)))
print("branches", len(branches), branches.count(True), branches.count(False))

# Another thread: the main thread enables tracing and monitoring while the
# worker spins in a loop that never returns to RESUME.
started = threading.Event()
stop = threading.Event()
seen_lines = []
seen_calls = []


def tracer2(frame, event, arg):
    if event == "call" and frame.f_code.co_name == "work":
        seen_calls.append(1)
    return None


def spin():
    started.set()
    n = 0
    while not stop.is_set():
        n = work(n) & 0xFFFF
    return n


def on_line2(code, line):
    if code.co_name == "spin":
        seen_lines.append(line)


t = threading.Thread(target=spin)
t.start()
started.wait()
threading.settrace_all_threads(tracer2)
while not seen_calls:
    pass
threading.settrace_all_threads(None)
mon.use_tool_id(TOOL, "midloop2")
mon.register_callback(TOOL, E.LINE, on_line2)
mon.set_events(TOOL, E.LINE)
while len(seen_lines) < 100:
    pass
mon.set_events(TOOL, 0)
mon.free_tool_id(TOOL)
stop.set()
t.join()
print("other thread traced calls", bool(seen_calls))
print("other thread lines", len(seen_lines) >= 100)
