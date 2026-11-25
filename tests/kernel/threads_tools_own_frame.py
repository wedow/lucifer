# Another thread turns a tool on while this one's compiled loop is blocked in
# a call it made, and off once this one says so: the loop's own events after
# the call returns (its lines, its return) begin at once, as on CPython.
# sys.monitoring events, then sys.setprofile (threading.setprofile_all_threads).
import sys
import threading

mon = sys.monitoring
E = mon.events
TOOL = 2
log = []


def watched(code):
    return code.co_name.startswith('g_')


mon.use_tool_id(TOOL, 'other thread')
mon.register_callback(TOOL, E.LINE, lambda code, line: watched(code) and log.append(('line', line - code.co_firstlineno)))
mon.register_callback(TOOL, E.PY_RETURN, lambda code, off, v: watched(code) and log.append(('return', code.co_name)))


def profiler(frame, event, arg):
    if watched(frame.f_code):
        log.append(('profile', event, frame.f_code.co_name))


def leaf(x):
    return x + 1


def g_loop(n, at, ready, go):
    s = 0
    for i in range(n):
        s += leaf(i)
        if i == at:
            ready.set()
            go.wait()
            s += 1
            s = s * 2
    return s


def switcher(tool, ready, go, done):
    ready.wait()
    if tool == 'monitoring':
        mon.set_events(TOOL, E.LINE | E.PY_RETURN)
    else:
        threading.setprofile_all_threads(profiler)
    go.set()
    done.wait()
    if tool == 'monitoring':
        mon.set_events(TOOL, 0)
    else:
        threading.setprofile_all_threads(None)


for tool in ('monitoring', 'setprofile'):
    for _ in range(3):
        g_loop(3000, -1, None, None)
    ready, go, done = threading.Event(), threading.Event(), threading.Event()
    t = threading.Thread(target=switcher, args=(tool, ready, go, done))
    t.start()
    r = g_loop(3000, 2900, ready, go)
    done.set()
    t.join()
    print(tool, r, log)
    log.clear()
mon.free_tool_id(TOOL)
