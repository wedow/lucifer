# A thread that exits with a trace and a profile function set no longer counts
# as tracing: once the main thread clears its own, no events stay on.
import sys
import threading


def tracer(frame, event, arg):
    return None


def worker():
    sys.settrace(tracer)
    sys.setprofile(tracer)


t = threading.Thread(target=worker)
t.start()
t.join()
sys.settrace(None)
sys.setprofile(None)
print(sys.monitoring._all_events())
