# Classes called with keywords in compiled loops: keywords, positionals
# with keywords, defaults, keyword-only parameters, **kwargs in __init__,
# a keyword __init__ does not take (the interpreter's TypeError), a class
# whose __new__ is its own (which sees the keywords), and a metaclass
# whose __call__ is its own. Every instance and error must be the
# interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class KW:
    def __init__(self, a, b=0, *, c=1):
        self.a = a
        self.b = b
        self.c = c


class Star:
    def __init__(self, a, **kw):
        self.a = a
        self.kw = kw


class New:
    def __new__(cls, *args, **kw):
        o = super().__new__(cls)
        o.seen = (args, sorted(kw.items()))
        return o

    def __init__(self, a, b=5):
        self.a = a
        self.b = b


class Meta(type):
    def __call__(cls, *args, **kw):
        o = super().__call__(*args, **kw)
        o.meta = True
        return o


class M(metaclass=Meta):
    def __init__(self, a, b=0):
        self.a = a
        self.b = b


def run(n):
    s = 0
    errors = []
    last = None
    for i in range(n):
        k = KW(i, c=i)
        k2 = KW(a=i, b=2)
        k3 = KW(i, 3, c=4)
        st = Star(i, x=i, y=2)
        nw = New(i, b=i)
        m = M(a=i, b=1)
        s += k.a + k.b + k.c + k2.a + k2.b + k2.c + k3.b + k3.c + st.a + st.kw['x'] + nw.b + m.a + m.b
        if i % 1000 == 0:
            try:
                KW(i, d=1)
            except TypeError as e:
                errors.append(str(e))
            try:
                KW(c=1)
            except TypeError as e:
                errors.append(str(e))
        last = (vars(k), vars(k2), vars(k3), sorted(vars(st).items(), key=str), nw.seen, vars(m))
    return s, errors, last


print(run(N))
