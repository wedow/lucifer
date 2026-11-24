# Instances released by compiled code while their class changes what a
# release must do: a class gaining and losing __del__ mid-loop, its base
# gaining one, __bases__ reassigned to a class with a finalizer, and a
# subclass of a class that needs nothing. Every finalizer due must run
# once, at the release, and every value the instances held must survive.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
log = []


def note(self):
    log.append(self.i)


class P:
    def __init__(self, i):
        self.i = i
        self.s = KEEP


KEEP = 'kept'


def churn(cls, n):
    keep = []
    for i in range(n):
        p = cls(i)
        if i % 500 == 0:
            keep.append(p.s)
    return len(keep), all(s is KEEP for s in keep)


def gain_lose(n):
    del log[:]
    class C:
        def __init__(self, i):
            self.i = i
            self.s = KEEP
    a = churn(C, n)
    C.__del__ = note
    b = churn(C, n)
    count = len(log)
    del C.__del__
    c = churn(C, n)
    return a, b, c, count, len(log) == count


def base_gains(n):
    del log[:]
    class Base:
        def __init__(self, i):
            self.i = i
            self.s = KEEP
    class D(Base):
        pass
    a = churn(Base, n), churn(D, n)
    Base.__del__ = note
    b = churn(D, n)
    return a, b, len(log)


def bases_reassigned(n):
    del log[:]
    class Plain:
        pass
    class Fin:
        def __del__(self):
            log.append(self.i)
    class E(Plain):
        def __init__(self, i):
            self.i = i
            self.s = KEEP
    a = churn(E, n)
    E.__bases__ = (Fin,)
    b = churn(E, n)
    return a, b, len(log)


def subclass(n):
    class F(P):
        pass
    class G(F):
        pass
    return churn(F, n), churn(G, n), churn(P, n)


print('gain and lose', gain_lose(N))
print('base gains', base_gains(N))
print('bases reassigned', bases_reassigned(N))
print('subclass', subclass(N))
