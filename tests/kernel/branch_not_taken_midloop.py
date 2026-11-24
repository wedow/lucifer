# A branch that falls through steps over the NOT_TAKEN after it; monitoring
# and line tracing switched on in the middle of the loop, from the loop's own
# thread and from another one, still see every branch and every line.
import sys
import threading

mon = sys.monitoring
TOOL = mon.DEBUGGER_ID
E = mon.events

events = []


def on_branch(kind):
    def cb(code, src, dst):
        if code.co_name in ("loop", "spin"):
            events.append((kind, code.co_name, src, dst))
    return cb


lines = []


def on_line(code, line):
    if code.co_name in ("loop", "spin"):
        lines.append((code.co_name, line - code.co_firstlineno))


def loop(n, switch):
    hits = [0, 0, 0, 0, 0]
    x = None
    for i in range(n):
        if i == switch:
            mon.use_tool_id(TOOL, "branches")
            mon.register_callback(TOOL, E.BRANCH_LEFT, on_branch("left"))
            mon.register_callback(TOOL, E.BRANCH_RIGHT, on_branch("right"))
            mon.register_callback(TOOL, E.LINE, on_line)
            mon.set_events(TOOL, E.BRANCH_LEFT | E.BRANCH_RIGHT | E.LINE)
        if i == switch + 6:
            mon.set_events(TOOL, 0)
            mon.free_tool_id(TOOL)
        if i & 1:
            hits[0] += 1
        if not i & 2:
            hits[1] += 1
        if x is None:
            hits[2] += 1
        if x is not None:
            hits[3] += 1
        x = None if i % 3 else i
        if i > 1 and i < n - 1:
            hits[4] += 1
    return hits


print(loop(100, 40))
print(len(events), sorted(set((k, s, d) for k, _, s, d in events)))
print(len(lines), sorted(set(lines)))

# Line events through sys.settrace switched on in the loop.
traced = []


def tracer(frame, event, arg):
    if frame.f_code.co_name == "loop":
        if event == "line":
            traced.append(frame.f_lineno - frame.f_code.co_firstlineno)
        return tracer
    return None


def toggled(n):
    total = 0
    for i in range(n):
        if i == 30:
            sys.settrace(tracer)
        if i == 33:
            sys.settrace(None)
        total += loop(4, -1)[0]
    return total


print(toggled(50), len(traced), sorted(set(traced)))

# Another thread turns monitoring on while this one spins in a branchy loop
# that never returns to RESUME, and off again once it has seen events.
events.clear()
lines.clear()
go = threading.Event()


def spin():
    n = 0
    k = 0
    while k < 3:
        if n & 1:
            n += 3
        else:
            n += 1
        if events:
            k += 1
    return n > 0


def enable():
    go.wait()
    mon.use_tool_id(TOOL, "other")
    mon.register_callback(TOOL, E.BRANCH_LEFT, on_branch("left"))
    mon.register_callback(TOOL, E.BRANCH_RIGHT, on_branch("right"))
    mon.set_events(TOOL, E.BRANCH_LEFT | E.BRANCH_RIGHT)


t = threading.Thread(target=enable)
t.start()
go.set()
print(spin())
t.join()
mon.set_events(TOOL, 0)
mon.free_tool_id(TOOL)
print(len(events) >= 3, set(k for k, *_ in events) <= {"left", "right"})
