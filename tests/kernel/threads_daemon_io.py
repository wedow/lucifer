# Daemon threads busy on the buffer under sys.stdout when the main program ends: the exit flushes the standard
# streams and does not wait for a lock that a daemon, stopped for good, still holds.
import sys
import threading
import time


def flusher():
    while True:
        sys.stdout.buffer.flush()


for _ in range(4):
    threading.Thread(target=flusher, daemon=True).start()
time.sleep(0.02)
print('main done')
