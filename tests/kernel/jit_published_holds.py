# The references a published slow path takes for the frame's slots and
# drops after its C returns, in loops that make no call of their own, so
# compiled code keeps the values in registers C may clobber: a truth test
# whose __bool__ runs Python that allocates and calls, a release whose
# finalizer does the same while another name still holds the object
# released, and a __bool__ that writes the caller's locals through
# f_locals (PEP 667). Counts must come back to where they were, and every
# result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 5000


def churn(k):
    # Enough Python to use every register C may: objects made, calls made.
    xs = [str(i) * 2 for i in range(k)]
    return len(sorted(xs, key=len)) + sum(map(len, xs))


class T:
    def __bool__(self):
        churn(4)
        return True


def truth(x, n):
    t = 0
    for i in range(n):
        if x:
            t += 1
    return t


o = T()
before = sys.getrefcount(o)
s = 0
for _ in range(N):
    s += truth(o, 50)
print('truth', s, sys.getrefcount(o) - before)


class P:
    def __del__(self):
        churn(4)


def released(n):
    t = 0
    keep = None
    for i in range(n):
        p = P()
        q = p
        p = None
        keep = q
        q = None
        t += i
    return t, keep is not None


print('released', [released(20) for _ in range(N // 10)][-1])


class W:
    def __bool__(self):
        f = sys._getframe(1)
        if f.f_locals['i'] == 30:
            f.f_locals['t'] = 1000
        return True


def written(x, n):
    t = 0
    for i in range(n):
        if x:
            t += 1
    return t


w = W()
print('written', [written(w, 50) for _ in range(N // 10)][-1], written(w, 50))
