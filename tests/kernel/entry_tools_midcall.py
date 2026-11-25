# A tool switched on, and off three calls later, from inside a call: one the
# caller only knows as an argument, one it can run in line, a generator's turn
# (a for loop and sum() consuming it), and a finalizer run as the callee
# starts. The tools are sys.settrace, sys.setprofile, sys.monitoring events,
# and local events on one code. The tool functions call hot user and kernel
# functions themselves. Events begin where CPython begins them; results are
# the same with the tool on as off.
import sys

mon = sys.monitoring
E = mon.events
TOOL = 2
N, ON = 400, 200
log = {}


def rec(*key):
    log[key] = log.get(key, 0) + 1


def work(n):
    return n * 2


def busy():
    return work(3) + sum(range(4)) + sorted([3, 1, 2])[0]


def tracer(frame, event, arg):
    name = frame.f_code.co_name
    if name.startswith('t_'):
        rec('trace', event, name, frame.f_lineno - frame.f_code.co_firstlineno)
        busy()
        return tracer
    return None


def on_start(code, offset):
    if code.co_name.startswith('t_'):
        rec('start', code.co_name)
        busy()


def on_line(code, line):
    if code.co_name.startswith('t_'):
        rec('line', code.co_name, line - code.co_firstlineno)


mon.use_tool_id(TOOL, 'midcall')
mon.register_callback(TOOL, E.PY_START, on_start)
mon.register_callback(TOOL, E.LINE, on_line)


def t_leaf(x):
    y = x + 1
    return y


def switch(tool, on):
    if tool == 'settrace':
        sys.settrace(tracer if on else None)
    elif tool == 'setprofile':
        sys.setprofile(tracer if on else None)
    elif tool == 'monitoring':
        mon.set_events(TOOL, E.PY_START | E.LINE if on else 0)
    elif tool == 'local':
        mon.set_local_events(TOOL, t_leaf.__code__, E.PY_START | E.LINE if on else 0)


def at(i, tool):
    if i == ON:
        switch(tool, True)
    elif i == ON + 3:
        switch(tool, False)


def t_direct(i, tool):
    at(i, tool)
    return t_leaf(i)


def t_inlined(i, tool):
    at(i, tool)
    return t_leaf(i)


def by_argument(fn, tool):
    s = 0
    for i in range(N):
        s += fn(i, tool)
    return s


def in_line(tool):
    s = 0
    for i in range(N):
        s += t_inlined(i, tool)
    return s


def t_gen(tool):
    for i in range(N):
        at(i, tool)
        yield t_leaf(i)


def gen_loop(tool):
    s = 0
    for v in t_gen(tool):
        s += v
    return s


class Arm:
    def __init__(self, tool, on):
        self.tool, self.on = tool, on

    def __del__(self):
        switch(self.tool, self.on)


def t_finalized(box, i):
    box.clear()
    return t_leaf(i)


def finalizer(tool):
    s = 0
    for i in range(N):
        box = [Arm(tool, i == ON)] if i in (ON, ON + 3) else []
        s += t_finalized(box, i)
    return s


WAYS = {
    'argument': lambda tool: by_argument(t_direct, tool),
    'in line': in_line,
    'generator': gen_loop,
    'generator sum': lambda tool: sum(t_gen(tool)),
    'finalizer': finalizer,
}
for _ in range(3):
    for run in WAYS.values():
        run('none')
for tool in ('settrace', 'setprofile', 'monitoring', 'local'):
    for way, run in WAYS.items():
        result = run(tool)
        print(tool, way, result, sorted(log.items()))
        log.clear()
mon.free_tool_id(TOOL)
