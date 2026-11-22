# As threads_serial_loop, but the second thread is a foreign one: made by
# pthread_create through ctypes, it becomes a thread of the runtime in its
# callback (PyGILState_Ensure) while the hot loop goes on storing, attached
# and compiled, without waiting for it. Each token is released exactly
# once: none while its pool still holds it, none kept after the pool lets
# it go.
import ctypes
import gc
import threading

N = 400000
START = 150000
freed = []


class Tok:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        freed.append(self.name)


class P:
    def __init__(self):
        self.i = 0
        self.f = 0.0
        self.o = None


MINE = [Tok(k) for k in range(16)]
THEIRS = [Tok(100 + k) for k in range(16)]


def other(ps, xs, n):
    for k in range(n):
        t = THEIRS[k & 15]
        p = ps[k & 7]
        p.i = t
        p.f = t
        p.o = t
        xs[k & 31] = t


libc = ctypes.CDLL(None)
libc.pthread_create.argtypes = [ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
libc.pthread_join.argtypes = [ctypes.c_ulong, ctypes.c_void_p]
THREAD_FN = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)
done = threading.Event()


class Foreign:
    def __init__(self, ps, xs):
        self.args = (ps, xs, N)
        self.fn = THREAD_FN(self.run)
        self.tid = ctypes.c_ulong()

    def run(self, arg):
        other(*self.args)
        done.set()
        return None

    def start(self):
        libc.pthread_create(ctypes.byref(self.tid), None, ctypes.cast(self.fn, ctypes.c_void_p), None)

    def join(self):
        done.wait()
        libc.pthread_join(self.tid.value, None)


def hot(ps, xs, n):
    th = None
    for k in range(n):
        t = MINE[k & 15]
        p = ps[k & 7]
        p.i = k
        p.f = k * 0.5
        p.o = t
        xs[k & 31] = t
        if k == START:
            th = Foreign(ps, xs)
            th.start()
    return th


def main():
    ps = [P() for _ in range(8)]
    xs = [None] * 32
    th = hot(ps, xs, N)
    th.join()
    kinds = {(type(p.i).__name__, type(p.f).__name__, type(p.o).__name__) for p in ps}
    print(kinds <= {(a, b, "Tok") for a in ("int", "Tok") for b in ("float", "Tok")},
          all(type(x) is Tok for x in xs))
    for p in ps:
        p.i = p.f = p.o = None
    xs[:] = []
    gc.collect()
    print("early", sorted(freed))
    th = None
    MINE.clear()
    THEIRS.clear()
    gc.collect()
    print("late", len(freed))


main()
