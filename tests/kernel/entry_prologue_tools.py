# A function with a cell and a free variable, hot and called from C
# (sorted's key, map), then called from C again while a tool watches its
# code: the call runs the cell's prologue once, as the interpreter does,
# and the tool sees each call. Then the tool goes and the calls go on.
import sys

def make(k):
    def f(x):
        y = x + k               # a cell: g reads it
        def g():
            return y * 2
        return g()
    return f

f = make(1)
for _ in range(3000):
    sorted([3, 1, 2], key=f)
    list(map(f, (4, 5)))

events = []

def profile(frame, event, arg):
    if frame.f_code.co_name in ('f', 'g'):
        events.append((event, frame.f_code.co_name))

for tool in ('setprofile', 'settrace'):
    events.clear()
    getattr(sys, tool)(profile)
    r = sorted([3, 1, 2], key=f), list(map(f, (4, 5)))
    getattr(sys, tool)(None)
    print(tool, r, events)
    print('after', sum(sorted(range(50), key=f)), sum(map(f, range(50))))

E = sys.monitoring.events
sys.monitoring.use_tool_id(3, 'probe')
seen = []
sys.monitoring.register_callback(3, E.PY_START, lambda code, off: seen.append(code.co_name))
sys.monitoring.set_events(3, E.PY_START)
r = sorted([3, 1, 2], key=f)
sys.monitoring.set_events(3, 0)
sys.monitoring.free_tool_id(3)
print('monitoring', r, [n for n in seen if n in ('f', 'g')])
print('after', sum(sorted(range(50), key=f)))
