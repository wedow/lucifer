# A frame that traces opcodes does not stop the world at every trace event:
# busy threads around it do not slow its tracing down.
import sys
import threading
import time

stop = False


def spin():
    x = 0
    while not stop:
        x += 1


def tracer(frame, event, arg):
    frame.f_trace_opcodes = True
    return tracer


def work():
    s = 0
    for i in range(20000):
        s += i
    return s


def traced():
    sys.settrace(tracer)
    t0 = time.perf_counter()
    result = work()
    t = time.perf_counter() - t0
    sys.settrace(None)
    return result, t


alone = traced()
spinners = [threading.Thread(target=spin) for _ in range(4)]
for t in spinners:
    t.start()
crowded = traced()
stop = True
for t in spinners:
    t.join()
print(alone[0] == crowded[0], crowded[1] < 4 * alone[1] + 0.05)
