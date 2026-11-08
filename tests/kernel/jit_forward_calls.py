# Calls the kernel forwards with f(a, *args, **kwargs): a class called
# through type.__call__ (its own __new__, or the class not a constant at
# the call), and operators whose method the dispatch finds at run time
# (call_special). Compiled code passes the arguments on as they are; an
# exception from the callee, keywords, and every way of calling must be
# the interpreter's. tools/jitcheck also runs this at two sizes and
# requires that the tuples, lists and dicts compiled code allocates do
# not grow with the iterations.
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    def __new__(cls, x, y=0):
        self = object.__new__(cls)
        self.n = 1
        return self

    def __init__(self, x, y=0):
        self.x = x + y

    def __add__(self, other):
        return self.x + other


class B(A):
    def __add__(self, other):
        return self.x * 2 + other


class C:
    def __init__(self, x, y=0):
        self.x = x - y

    def __add__(self, other):
        return self.x - other


class D(C):
    def __radd__(self, other):
        return other + 100


KINDS = [A, B, C, D]


def make(i):
    cls = KINDS[i % 4]
    return cls(i)


def step(i):
    o = make(i)
    s = o + i            # the dispatch meets four classes here
    s += 1 + KINDS[3](i)
    return s


step(0)
step(1)
step(2)
step(3)


def hot(n):
    s = 0
    for i in range(n):
        s += step(i)
    return s


print('hot', hot(N))

K = 300


def keywords():
    out = []
    for i in range(K):
        cls = KINDS[i % 4]
        o = cls(i, y=3)
        out.append(o.x)
    return sum(out)


print('keywords', keywords())


class Bad:
    def __init__(self, x):
        if x == 7:
            raise ValueError('seven')
        self.x = x

    def __add__(self, other):
        if other == 9:
            raise KeyError(other)
        return self.x


def raises():
    out = []
    kinds = [Bad, A]
    for i in range(K):
        try:
            o = kinds[i % 2](i % 11)
            out.append(o + (i % 13))
        except (ValueError, KeyError) as e:
            tb = traceback.extract_tb(e.__traceback__)
            out.append((type(e).__name__, [f.name for f in tb], tb[-1].line))
    return out[:3], out[-3:], len(out)


print('raises', raises())


def counts():
    made = []

    class E:
        def __new__(cls, *args, **kwargs):
            made.append((args, sorted(kwargs)))
            return object.__new__(cls)

        def __init__(self, *args, **kwargs):
            self.args = args

    for i in range(K):
        E(i, i + 1, k=i) if i % 5 == 0 else E(i)
    return len(made), made[-1], made[-2]


print('counts', counts())
