# Tools switched on from the main thread while a worker runs hot calls:
# loop-free recursion through a direct call, and a callee that C calls
# (sorted's key). sys.monitoring PY_START and threading.settrace_all_threads
# reach the worker's next calls, as on CPython.
import sys
import threading
import time

mon = sys.monitoring
TOOL = mon.PROFILER_ID
E = mon.events


def down(n):
    return 0 if n == 0 else down(n - 1) + 1


def keyf(x):
    return -x


DATA = list(range(30))
WORK = {
    'recursion': (down, lambda: down(40)),
    'C callee': (keyf, lambda: sorted(DATA, key=keyf)),
}


def watch(name, switch_on, switch_off, seen):
    target, run = WORK[name]
    started = threading.Event()
    stop = threading.Event()

    def worker():
        for _ in range(300):
            run()
        started.set()
        while not stop.is_set():
            run()

    t = threading.Thread(target=worker)
    t.start()
    started.wait()
    switch_on()
    while len(seen) < 100:
        time.sleep(0.001)
    switch_off()
    stop.set()
    t.join()
    return len(seen) >= 100 and all(code is target.__code__ for code in seen[:100])


for name in WORK:
    seen = []
    code = WORK[name][0].__code__

    def on_start(c, offset, seen=seen, code=code):
        if c is code:
            seen.append(c)

    mon.use_tool_id(TOOL, 'threads')
    mon.register_callback(TOOL, E.PY_START, on_start)
    print(name, 'monitoring', watch(name, lambda: mon.set_events(TOOL, E.PY_START),
                                    lambda: mon.set_events(TOOL, 0), seen))
    mon.free_tool_id(TOOL)

    seen = []

    def tracer(frame, event, arg, seen=seen, code=code):
        if event == 'call' and frame.f_code is code:
            seen.append(frame.f_code)
        return None

    print(name, 'settrace', watch(name, lambda: threading.settrace_all_threads(tracer),
                                  lambda: threading.settrace_all_threads(None), seen))
