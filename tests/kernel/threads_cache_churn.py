"""Threads replace and drop what hot loops in other threads have cached or
compiled against: a global function, a method, a class attribute and a
method that comes and goes. Each replacement drops the last reference to the
value it replaces, so an inline cache or compiled code that kept the old one
without owning it would use freed memory. The readers compile fresh copies
of their loop again and again, so compiles meet the changes too. Every call
must return a value from before or after a replacement."""
import threading

ROUNDS = 150
SOURCE = '''
def hot(n, o):
    t = 0
    for _ in range(n):
        t += helper(1) + C.k + o.k + o.m()
        try:
            t += o.n()
        except AttributeError:
            pass
    return t
'''


def helper(x):
    return 0


class C:
    k = 0

    def m(self):
        return 0

    def n(self):
        return 0


def method(i):
    return lambda self: i


def function(i):
    return lambda x: i


stop = threading.Event()
# Objects of a function's size, allocated as the replaced values die, so that
# freed memory soon holds something else.
ring = [None] * 512


def writer():
    global helper
    i = 0
    while not stop.is_set():
        i += 1
        helper = function(i)
        C.m = method(i)
        C.k = i * 1000
        del C.n
        C.n = method(i)
        ring[i % len(ring)] = tuple(range(16))


def reader(out):
    bad = 0
    o = C()
    for _ in range(ROUNDS):
        exec(SOURCE, globals())
        if hot(1500, o) < 0:
            bad += 1
    out.append(bad)


out = []
readers = [threading.Thread(target=reader, args=(out,)) for _ in range(3)]
w = threading.Thread(target=writer)
w.start()
for t in readers:
    t.start()
for t in readers:
    t.join()
stop.set()
w.join()
print('bad', sum(out), len(out))
print(helper(1) > 0, C().m() > 0, C.k > 0, C().n() > 0)
