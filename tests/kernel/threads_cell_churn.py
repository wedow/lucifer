"""A thread rebinds a closure's variable (STORE_DEREF) while others load and
call it (LOAD_DEREF), and another reads it through the frame's locals. Each
rebinding drops the last reference to a closure no other thread has
referenced, so a load that took its reference without the cell's lock, or a
store that replaced the value without it, would use freed memory. Every call
must return a value from before or after a rebinding."""
import sys
import threading

READS = 1000000
stop = threading.Event()
# Objects of a function's size, allocated as the replaced values die, so that
# freed memory soon holds something else.
ring = [None] * 256


def function(i):
    return lambda: i


def closure():
    value = function(1)

    def store():
        nonlocal value
        i = 1
        while not stop.is_set():
            i += 1
            value = function(i)
            ring[i % len(ring)] = (i,) * 16

    def load(out):
        bad = 0
        for _ in range(READS):
            if value() < 1:
                bad += 1
        out.append(bad)

    def snapshot(out):
        bad = 0
        frame = sys._getframe()
        for _ in range(READS // 100):
            if frame.f_locals['value']() < 1:
                bad += 1
        out.append(bad + (value is None))
    return store, load, snapshot


store, load, snapshot = closure()
out = []
writer = threading.Thread(target=store)
readers = [threading.Thread(target=r, args=(out,)) for r in (load, load, snapshot)]
writer.start()
for t in readers:
    t.start()
for t in readers:
    t.join()
stop.set()
writer.join()
print('bad', sum(out), len(out))
