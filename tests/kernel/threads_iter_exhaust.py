# Iterators one thread steps while other threads pick them up, step them and
# exhaust them: every item stepped must be a live item of the sequence.
import threading


class O:
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = v


slot = [None]
stop = [False]
bad = []


def check(it):
    for o in it:
        if type(o) is not O or o.v < 0:
            bad.append(repr(type(o)))


def consumer():
    while not stop[0]:
        it = slot[0]
        if it is not None:
            check(it)


def producer(n):
    for r in range(n):
        it = iter([O(i) for i in range(8)]) if r & 1 else iter(tuple(O(i) for i in range(8)))
        slot[0] = it
        check(it)
        slot[0] = None
        del it


cs = [threading.Thread(target=consumer) for _ in range(3)]
for t in cs:
    t.start()
producer(100000)
stop[0] = True
for t in cs:
    t.join()
print('bad', len(bad), bad[:3])
