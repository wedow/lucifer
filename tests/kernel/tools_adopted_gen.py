# A tool turned on from inside a generator that a kernel consumer (sum, list,
# sorted) runs, and off three turns later, again and again in one process:
# the generator's turns report their resumes and yields as on CPython each
# time, though the consumer's compiled code was built, with the generator in
# line, before the tool went on. sys.setprofile and sys.monitoring events.
import sys

mon = sys.monitoring
E = mon.events
TOOL = 2
N, ON = 400, 200
log = {}


def rec(*key):
    log[key] = log.get(key, 0) + 1


def profiler(frame, event, arg):
    if frame.f_code.co_name.startswith('t_'):
        rec('profile', event, frame.f_code.co_name)


mon.use_tool_id(TOOL, 'adopted')
for event, name in ((E.PY_RESUME, 'resume'), (E.PY_YIELD, 'yield'), (E.PY_START, 'start')):
    mon.register_callback(TOOL, event, lambda code, off, *rest, name=name:
                          code.co_name.startswith('t_') and rec(name, code.co_name))


def switch(tool, on):
    if tool == 'setprofile':
        sys.setprofile(profiler if on else None)
    elif tool == 'monitoring':
        mon.set_events(TOOL, E.PY_RESUME | E.PY_YIELD | E.PY_START if on else 0)


def at(i, tool):
    if i == ON:
        switch(tool, True)
    elif i == ON + 3:
        switch(tool, False)


def t_leaf(x):
    return x + 1


def t_gen(tool):
    for i in range(N):
        at(i, tool)
        yield t_leaf(i)


CONSUMERS = {'sum': sum, 'list': lambda g: len(list(g)), 'sorted': lambda g: sorted(g)[-1]}
for _ in range(3):
    for consume in CONSUMERS.values():
        consume(t_gen('none'))
for tool in ('setprofile', 'monitoring'):
    for name, consume in CONSUMERS.items():
        for k in range(3):
            print(tool, name, k, consume(t_gen(tool)), sorted(log.items()))
            log.clear()
mon.free_tool_id(TOOL)
