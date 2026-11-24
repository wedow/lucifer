# Instances released by compiled code (each escapes into a list slot and
# is released when the slot is overwritten) while their class changes what
# a release must do: a class gaining and losing __del__ mid-loop, a base
# class gaining one (released as itself and through a subclass), a
# __bases__ assignment refused or taking a base with a finalizer, and
# subclasses. The instances hold counted values with finalizer probes:
# one only every other instance holds (it dies with the instance; the
# others hold the shared one there), one held two or three times by each
# instance and once outside (it must survive), and the instance's own
# class (the release's reference to the type comes on top). Every
# finalizer due must run once, at the release, and none early.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
log = []
probes = []


class Probe:
    def __del__(self):
        probes.append(1)


def note(self):
    log.append(1)


def make(base=object, name='C'):
    def __init__(self, i, shared):
        self.i = i
        self.own = Probe() if i % 2 else shared     # dies with the instance, or a third reference
        self.a = shared                 # twice in each instance, once outside
        self.b = shared
        self.cls = type(self)           # the type twice: the attribute and the release's own
    return type(name, (base,), {'__init__': __init__})


def churn(cls, n):
    shared = Probe()
    out = [None] * 7
    before = len(probes)
    for i in range(n):
        out[i % 7] = cls(i, shared)
    out = None
    died = len(probes) - before         # each odd instance's own probe, and not the shared one
    shared = None
    return died, len(probes) - before - died == 1


def gain_lose(n):
    del log[:]
    C = make()
    a = churn(C, n)
    C.__del__ = note
    b = churn(C, n)
    count = len(log)
    del C.__del__
    c = churn(C, n)
    return a, b, c, count, len(log) == count


def base_gains(n):
    del log[:]
    Base = make(name='Base')
    D = make(Base, 'D')
    a = churn(Base, n), churn(D, n)
    Base.__del__ = note
    b = churn(Base, n), churn(D, n)
    return a, b, len(log)


def bases_assigned(n):
    del log[:]
    class Fin:
        def __del__(self):
            log.append(1)
    E = make(name='E')
    try:
        E.__bases__ = (Fin,)
        refused = False
    except TypeError:
        refused = True
    a = churn(E, n)
    Plain = make(name='Plain')
    E2 = make(Plain, 'E2')
    b = churn(E2, n)
    E2.__bases__ = (Fin,)
    c = churn(E2, n)
    return refused, a, b, c, len(log)


def subclass(n):
    P = make(name='P')
    F = make(P, 'F')
    G = make(F, 'G')
    return churn(F, n), churn(G, n), churn(P, n)


print('gain and lose', gain_lose(N))
print('base gains', base_gains(N))
print('bases assigned', bases_assigned(N))
print('subclass', subclass(N))
print('probes', len(probes))
