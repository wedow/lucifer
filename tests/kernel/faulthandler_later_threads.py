# Threads arming and cancelling dump_traceback_later (no GIL): one watchdog at a time, and none
# left behind after the last cancel. An arm and its cancel are made one thread at a time: the
# free-threaded oracle deadlocks (about 1 run in 25) when threads arm and cancel at once.
import faulthandler, os, threading, time
gate = threading.Lock()
r, w = os.pipe()
os.set_blocking(r, False)


def arm():
    for i in range(300):
        with gate:
            faulthandler.dump_traceback_later(0.2, file=w)
            faulthandler.cancel_dump_traceback_later()


ts = [threading.Thread(target=arm) for _ in range(8)]
for t in ts:
    t.start()
for t in ts:
    t.join()
faulthandler.cancel_dump_traceback_later()
time.sleep(0.4)
try:
    data = os.read(r, 65536)
except BlockingIOError:
    data = b""
print("dumped" if data else "quiet")
