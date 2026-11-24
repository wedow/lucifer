# The ways out of a published call whose holds are one count step for
# an object several frame slots name: a callee that makes the caller's
# frames real (reads and writes its locals through f_locals), one that
# rebinds a global the caller's unit relies on (the unit dies mid-call),
# a release that calls C with the registers saved (another thread's
# object with a finalizer, dropped from one of the slots naming it), an
# object this thread owns whose last local references are the holds when
# they go (its other reference is another thread's, so the holds drop in
# C), and a second thread changing the object's count during the calls.
# Counts come back where they were, nothing is freed early or twice, and
# every result is the interpreter's.
import sys
import threading

grc = sys.getrefcount
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
FREED = []


class C:
    pass


class Fin:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        FREED.append(self.k)


def elsewhere(make):
    box = []
    th = threading.Thread(target=lambda: box.append(make()))
    th.start()
    th.join()
    return box.pop()


def peek(x):
    f = sys._getframe(1).f_locals
    return f['a'] is f['c'] and f['c'] is x


def framed(o, n):
    t = 0
    for i in range(n):
        a = o
        b = a
        c = b
        t += peek(c)
    return t == n


def poke(x, i):
    if i % 97 == 0:
        sys._getframe(1).f_locals['t'] = -1000000
    return grc(x) > 0


def written(o, n):
    t = 0
    for i in range(n):
        a = o
        b = a
        c = b
        t += poke(c, i)
    return t


def one():
    return 1


def two():
    return 2


step = one


def flip(x, i):
    global step
    if i % 101 == 100:
        step = two if step is one else one
    return grc(x) > 0


def invalidated(o, n):
    t = 0
    for i in range(n):
        a = o
        b = a
        c = b
        t += flip(c, i) + step()
    return t


def released(o, n):
    for i in range(n):
        a = o
        b = o
        c = o
        a = None
        b = None
    return c is o


def drop(lst, i):
    lst[i] = None
    return 1


def last_local(mine_refs, theirs_refs, n):
    t = 0
    for i in range(n):
        o = theirs_refs[i]
        a = o
        b = a
        t += drop(mine_refs, i) + (b is o)
    return t


def contended(o, n, stop):
    t = 0
    for i in range(n):
        a = o
        b = a
        c = b
        t += grc(c) > 0
    stop.append(1)
    return t


mine = C()
theirs = elsewhere(C)
before = (grc(mine), grc(theirs))
for label, o in (('mine', mine), ('theirs', theirs)):
    print(label, framed(o, N // 10), written(o, N // 10), invalidated(o, N // 10))
del label, o
fin = elsewhere(lambda: Fin(-1))
print('released', released(fin, N // 10), FREED)
print('counts back', (grc(mine), grc(theirs)) == before)

# Objects this thread made and names once (mine_refs); another thread
# names each too (its references are shared counts).
K = N // 10
mine_refs = [Fin(k) for k in range(K)]
theirs_refs = []
th = threading.Thread(target=lambda: theirs_refs.extend(mine_refs))
th.start()
th.join()
print('last local', last_local(mine_refs, theirs_refs, K) == 2 * K, len(FREED))
theirs_refs.clear()
print('freed once', sorted(FREED) == list(range(K)))
FREED.clear()

# A second thread counts the object up and down while the loop holds it.
# (Its count right after the thread ends is not compared: the releases
# another thread made may still wait for this thread to merge them.)
stop = []
shared = C()


def churn():
    keep = []
    while not stop:
        keep.append(shared)
        if len(keep) > 50:
            keep.clear()


th = threading.Thread(target=churn)
th.start()
r = contended(shared, N, stop)
th.join()
print('contended', r == N)
del fin
print('end', FREED)
