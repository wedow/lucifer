"""Threads replace globals, by STORE_GLOBAL and by STORE_NAME in module-level
code, while others load and call them. Each replacement drops the last
reference to a closure no other thread has referenced, so a reader's
lock-free try fails and it reads under the globals' lock: a store that did
not take that lock could free the value the reader is taking. Every call
must return a value from before or after a replacement."""
import threading

READS = 1000000
STORE_NAME = compile('named = function(1)', '<module>', 'exec')


def function(i):
    return lambda: i


stored = named = function(1)
stop = threading.Event()
# Objects of a function's size, allocated as the replaced values die, so that
# freed memory soon holds something else.
ring = [None] * 256


def store_global():
    global stored
    i = 0
    while not stop.is_set():
        i += 1
        stored = function(i)
        ring[i % len(ring)] = (i,) * 16


def store_name():
    while not stop.is_set():
        exec(STORE_NAME, globals())


def read_stored(out):
    bad = 0
    for _ in range(READS):
        if stored() < 1:
            bad += 1
    out.append(bad)


def read_named(out):
    bad = 0
    for _ in range(READS):
        if named() < 1:
            bad += 1
    out.append(bad)


out = []
writers = [threading.Thread(target=store_global), threading.Thread(target=store_name)]
readers = [threading.Thread(target=r, args=(out,)) for r in (read_stored, read_stored, read_named)]
for t in writers + readers:
    t.start()
for t in readers:
    t.join()
stop.set()
for t in writers:
    t.join()
print('bad', sum(out), len(out))
