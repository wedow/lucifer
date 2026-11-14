# Calls compiled code binds itself: positional and keyword arguments,
# defaults, keyword-only parameters, rest tuples and keyword dicts, through
# functions, methods and closures; defaults changed between calls; and the
# calls whose binding raises, which must raise the interpreter's error
# from the interpreter's place.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def plain(a, b, c=3, d=4):
    return a + b * c - d


def rest(a, *args):
    return a + len(args) + (args[0] if args else 0) + sum(args)


def keyword_only(a, *, scale=2, shift=0):
    return a * scale + shift


def everything(a, b=1, /, c=2, *args, d=5, **kwargs):
    return a + b + c + len(args) + d + len(kwargs) + sum(kwargs.values())


def retained(*args, **kwargs):
    return args, kwargs


class Shape:
    def __init__(self, w, h=2):
        self.w = w
        self.h = h

    def area(self, scale=1, *, extra=0):
        return self.w * self.h * scale + extra

    @staticmethod
    def static(a, b=10):
        return a - b

    @classmethod
    def make(cls, w):
        return cls(w, 3)


def closure(k):
    def add(x, y=k):
        return x + y + k
    return add


def calls(n):
    s = 0
    shape = Shape(3)
    add = closure(5)
    for i in range(n):
        s += plain(i, 2)
        s += plain(i, 2, 5)
        s += plain(i, b=2, d=i)
        s += plain(d=1, c=2, b=3, a=i)
        s += rest(i)
        s += rest(i, 1, 2, 3)
        s += keyword_only(i)
        s += keyword_only(i, shift=i, scale=3)
        s += everything(i)
        s += everything(i, 2, 3, 4, 5, d=6)
        s += everything(i, d=1, x=2, y=3)
        s += shape.area()
        s += shape.area(2, extra=i)
        s += Shape.static(i)
        s += Shape.make(i).area(extra=1)
        s += add(i)
        s += add(i, y=1)
    return s


def changed_defaults(n):
    out = []
    for i in range(n):
        if i == n - 20:
            plain.__defaults__ = (30, 40)
        if i == n - 10:
            keyword_only.__kwdefaults__ = {'scale': 5, 'shift': 1}
        if i >= n - 25 and i % 4 == 0:
            out.append((plain(i, 1), keyword_only(i)))
    plain.__defaults__ = (3, 4)
    keyword_only.__kwdefaults__ = {'scale': 2, 'shift': 0}
    return out


def kept(n):
    out = []
    for i in range(n):
        a, k = retained(i, i + 1, tag=i)
        if i >= n - 2:
            k['more'] = a
            out.append((a, sorted(k.items(), key=str)))
    return out


def failing(n):
    out = []
    for i in range(n):
        if i < n - 6:
            continue
        for call in (lambda: plain(i), lambda: plain(i, 1, 2, 3, 4), lambda: plain(i, 1, a=2),
                     lambda: plain(i, 1, e=5), lambda: keyword_only(i, 2), lambda: rest(),
                     lambda: everything(a=1), lambda: Shape(1).area(1, 2)):
            try:
                out.append(call())
            except TypeError as e:
                tb = e.__traceback__
                depth = 0
                while tb.tb_next is not None:
                    tb = tb.tb_next
                    depth += 1
                out.append((str(e), depth))
    return out


print(calls(N))
print(changed_defaults(N))
print(kept(N))
for line in failing(N):
    print(line)
