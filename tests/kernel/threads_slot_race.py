"""Threads store into and read one __slots__ member of a shared instance,
through the specialized slot instructions and the generic member descriptor.
Each store drops the last reference to the value it replaces: a read returns
a value from before or after a store, never a freed one, and no value is
released twice."""
import threading

ROUNDS = 20000


class S:
    __slots__ = ('x',)


s = S()
s.x = 0.0


def writer(k):
    for i in range(ROUNDS):
        s.x = float(i)
        if i % 64 == 0:
            S.x.__set__(s, float(k))


def reader(out):
    bad = 0
    for _ in range(ROUNDS):
        v = s.x
        if not 0.0 <= v < ROUNDS:
            bad += 1
        if not 0.0 <= S.x.__get__(s) < ROUNDS:
            bad += 1
    out.append(bad)


out = []
threads = [threading.Thread(target=writer, args=(k,)) for k in range(3)]
threads += [threading.Thread(target=reader, args=(out,)) for _ in range(2)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print('bad', sum(out), len(out), type(s.x).__name__)
