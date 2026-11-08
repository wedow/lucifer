"""A thread reassigns a class's __bases__, which replaces its MRO tuple and
drops the old one, while other threads look an attribute up through that
class. A lookup walks the MRO without a lock, so a walk that still held the
old tuple after it was freed would read whatever took its memory. The
class's version tags are spent first, so that every lookup walks the MRO
rather than hitting the attribute cache, and there are more readers than
cores, so that walks are preempted midway. Every lookup must find the
attribute in one MRO or the other."""
import threading

DEPTH = 40
SWAPS = 200
READERS = 16


def chain(k):
    base = type('Root', (), {'k': k})
    for i in range(DEPTH):
        base = type('L%d' % i, (base,), {})
    return base


B1, B2 = chain(1), chain(2)


class C(B1):
    pass


# A lookup gives C a tag and a change takes it back; past the limit of tags
# per class, C is never cached again.
for i in range(1100):
    C.x = i
    C.x

stop = threading.Event()
size = len(C.__mro__)
# Tuples of the MRO's size, allocated as old MROs die, so that freed memory
# soon holds something else.
ring = [None] * 256


def writer():
    for i in range(SWAPS):
        C.__bases__ = (B2,) if i & 1 else (B1,)
        ring[i % len(ring)] = tuple(range(size))
    stop.set()


def reader(out):
    o = C()
    bad = 0
    while not stop.is_set():
        if o.k not in (1, 2):
            bad += 1
    out.append(bad)


out = []
w = threading.Thread(target=writer)
readers = [threading.Thread(target=reader, args=(out,)) for _ in range(READERS)]
for t in readers:
    t.start()
w.start()
w.join()
for t in readers:
    t.join()
print('bad', sum(out), len(out) == READERS)
