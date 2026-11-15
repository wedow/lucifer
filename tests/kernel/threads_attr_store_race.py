# Threads racing on the same instance's number attributes: compiled code
# stores and reads ints and floats (unboxed where it can) while other
# threads store objects of other kinds, read the values into locals and
# calls, take the instance's __dict__, delete and restore an attribute,
# and reassign the class. Nothing is lost that one thread alone stored
# last, every value read is one some thread stored, and a value read
# twice in a row from a quiet attribute is one object.
import threading

ROUNDS = 40
N = 3000


class P:
    def __init__(self):
        self.i = 0
        self.f = 0.0
        self.mine = 0


class Q(P):
    pass


def writer(p, n, base):
    # Only this thread writes p.mine: it must read back what it wrote.
    wrong = 0
    for k in range(n):
        p.i = base + k
        p.f = (base + k) * 0.5
        p.mine = p.mine + 1
        if p.mine != k + 1:
            wrong += 1
    return wrong


def reader(p, n, allowed_lo, allowed_hi, out):
    bad = 0
    for k in range(n):
        try:
            a = p.i
            b = p.f
            c = abs(p.i)
        except AttributeError:
            continue            # between the shaper's del and its store
        if type(a) is not int or type(b) is not float:
            bad += 1
        elif not (allowed_lo <= a < allowed_hi and allowed_lo <= b * 2 < allowed_hi and c == abs(c)):
            bad += 1
    out.append(bad)


def boxer(p, n, out):
    # Stores the objects it read back, as objects.
    for k in range(n):
        x = p.i
        y = p.f
        p.i = int(x)
        p.f = float(y)
    out.append(0)


def shaper(p, n, out):
    # Changes how the instance keeps its values.
    seen = 0
    for k in range(n // 50):
        d = p.__dict__
        seen += ('i' in d) + ('f' in d)
        try:
            del p.f
        except AttributeError:
            pass
        p.f = 0.0
        p.__class__ = Q if k % 2 else P
    out.append(seen >= 0)


def run(extra):
    total = 0
    for r in range(ROUNDS):
        p = P()
        writer(p, 500, 0)       # warm: the stores are compiled
        p.mine = 0
        out = []
        threads = [threading.Thread(target=reader, args=(p, N, 0, 10 ** 7, out)),
                   threading.Thread(target=extra, args=(p, N, out))]
        for t in threads:
            t.start()
        wrong = writer(p, N, 1000)
        for t in threads:
            t.join()
        total += wrong + sum(1 for x in out if x not in (0, True))
        if p.mine != N:
            total += 1
        if extra is boxer and not (p.i is p.i and p.f is p.f):
            total += 1
    return total


print('boxer', run(boxer))
print('shaper', run(shaper))
