# Finalizers that run as a hot callee is entered or returns: one that walks
# the frames, one that rebinds a global the callee reads, one that replaces
# the callee's __code__, one that raises; a collection due as the callee
# starts; and a global value the callee read, released once it is rebound.
import gc
import sys
import weakref

log = []
sys.unraisablehook = lambda u: log.append('unraisable ' + type(u.exc_value).__name__)
K = 1


class Fin:
    def __init__(self, act):
        self.act = act

    def __del__(self):
        self.act()


def frames():
    f, out = sys._getframe(2), []
    while f is not None and f.f_code.co_name != '<module>':
        out.append(f.f_code.co_name)
        f = f.f_back
    log.append(out)


def rebind():
    global K
    K += 10


def other(box, x):
    return -x


def recode():
    callee.__code__ = other.__code__


def boom():
    raise KeyError('in __del__')


def callee(box, x):
    box.clear()        # a finalizer runs as the callee starts
    return x + K


def returning(x):
    f = Fin(ACT[0])    # a finalizer runs as the callee returns
    return x + K


def drive(fn, n, at, act):
    s = 0
    for i in range(n):
        if fn is callee:
            box = [Fin(act)] if i == at else []
            s += fn(box, i)
        else:
            ACT[0] = act if i == at else (lambda: None)
            s += fn(i)
    return s


ACT = [lambda: None]
code = callee.__code__
for fn in (callee, returning):
    for name, act in (('frames', frames), ('rebind', rebind), ('recode', recode), ('raise', boom)):
        drive(fn, 300, -1, None)
        K = 1
        print(fn.__name__, name, drive(fn, 400, 200, act), log)
        log.clear()
        callee.__code__ = code


# A collection due as the callee starts, collecting a cycle with a finalizer.
class Cycle:
    def __init__(self):
        self.me = self

    def __del__(self):
        log.append('cycle')


def starts(x):
    return x + 1


def collecting(n):
    s = 0
    for i in range(n):
        if i == 200:
            Cycle()
            gc.collect()
        s += starts(i)
    return s


gc.disable()
print('collected', collecting(400), log)
gc.enable()
log.clear()


# A global value the hot callee read is released once the global is rebound.
class Value:
    def __add__(self, other):
        return other


V = Value()
ref = weakref.ref(V)


def reads(x):
    return V + x


def loop(n):
    s = 0
    for i in range(n):
        s += reads(i)
    return s


print(loop(500))
V = 0
print(loop(10), ref() is None)
gc.collect()
print('released', ref() is None)
