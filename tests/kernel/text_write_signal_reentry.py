"""A signal handler that writes to the stream the interrupted write is
flushing never waits for its own thread: the handler's write goes through
or raises RuntimeError (luc-g2r0)."""
import os
import signal
import tempfile
import time

with tempfile.TemporaryDirectory() as d:
    f = open(os.path.join(d, "t"), "w", encoding="utf-8")
    f.reconfigure(write_through=True)

    def handler(signum, frame):
        try:
            f.write("h\n")
        except RuntimeError:
            pass

    signal.signal(signal.SIGALRM, handler)
    signal.setitimer(signal.ITIMER_REAL, 0.002, 0.002)
    end = time.monotonic() + 3
    while time.monotonic() < end:
        try:
            f.write("y\n")
        except RuntimeError:
            pass
    signal.setitimer(signal.ITIMER_REAL, 0)
    f.close()
print("done")
