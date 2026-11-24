# Classes called with keywords in hot compiled loops, each case many
# times: keywords, positionals with keywords, defaults, keyword-only
# parameters, **kwargs in __init__; every binding error (an unexpected
# keyword, a duplicate, a missing positional, a missing keyword-only
# without default, self= as a keyword, a class with no __init__ given a
# keyword), with the interpreter's messages; a positional-only self with
# **kw receiving 'self'; an abstract class; __init__ returning a value;
# __init__ raising midway; __init__, __new__, __defaults__ and
# __kwdefaults__ changed after warm-up; __del__ on the class; __new__ and
# a metaclass __call__ of their own; __slots__, a dataclass, an Enum and
# subclasses of int, dict, tuple and Exception called with keywords.
import abc
import dataclasses
import enum
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000


class KW:
    def __init__(self, a, b=0, *, c=1):
        self.a = a
        self.b = b
        self.c = c


class Star:
    def __init__(self, a, **kw):
        self.a = a
        self.kw = kw


class PosOnly:
    def __init__(self, /, a, **kw):
        self.a = a
        self.kw = kw


class KwOnly:
    def __init__(self, *, need):
        self.need = need


class NoInit:
    pass


class Abstract(abc.ABC):
    @abc.abstractmethod
    def f(self):
        pass

    def __init__(self, a=0):
        self.a = a


class Returns:
    def __init__(self, a=0):
        self.a = a
        return 5


class Raises:
    def __init__(self, a, b=0):
        self.a = a
        if a % 7 == 0:
            raise ValueError(a)
        self.b = b


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


deleted = []


class Del:
    def __init__(self, a, b=0):
        self.a = a
        self.b = b

    def __del__(self):
        deleted.append(self.a)


class Slots:
    __slots__ = ('a', 'b')

    def __init__(self, a, b=0):
        self.a = a
        self.b = b


@dataclasses.dataclass
class DC:
    a: int
    b: int = 0


class Color(enum.Enum):
    RED = 1
    GREEN = 2


class I(int):
    pass


class D(dict):
    pass


class T(tuple):
    pass


class E(Exception):
    def __init__(self, *args, code=0):
        super().__init__(*args)
        self.code = code


class Change:
    def __init__(self, a, b=1, *, c=2):
        self.v = (a, b, c)


def errors(f):
    out = set()
    for i in range(N):
        try:
            f(i)
        except TypeError as e:
            out.add(str(e))
    return sorted(out)


def run():
    s = 0
    for i in range(N):
        s += KW(i, c=i).c + KW(a=i, b=2).b + KW(i, 3, c=4).a + Star(i, x=i).kw['x'] + PosOnly(a=i, self=7).kw['self']
        s += KwOnly(need=i).need + New(i, b=i).b + M(a=i, b=1).a + Slots(i, b=2).b + DC(a=i, b=1).b
        s += I(3) + len(D(x=1, y=i)) + len(T((1, 2)))
    out = [s]
    out.append(errors(lambda i: KW(i, d=1)))
    out.append(errors(lambda i: KW(1, a=2)))
    out.append(errors(lambda i: KW(c=1)))
    out.append(errors(lambda i: KW(self=1, a=2)))
    out.append(errors(lambda i: KwOnly()))
    out.append(errors(lambda i: NoInit(x=1)))
    out.append(errors(lambda i: Abstract(a=1)))
    out.append(errors(lambda i: Returns(a=i)))
    raised = 0
    for i in range(N):
        try:
            Raises(a=i, b=1)
        except ValueError:
            raised += 1
    out.append(raised)
    for i in range(N):
        Del(i, b=1)
    out.append((len(deleted), sum(deleted)))
    seen = []
    for phase in range(5):
        if phase == 1:
            Change.__init__.__defaults__ = (10,)
        elif phase == 2:
            Change.__init__.__kwdefaults__ = {'c': 20}
        elif phase == 3:
            def init(self, a, b=100, *, c=200):
                self.v = ('new', a, b, c)
            Change.__init__ = init
        elif phase == 4:
            def new(cls, *args, **kw):
                o = object.__new__(cls)
                o.made = kw
                return o
            Change.__new__ = new
        for i in range(N):
            odd = Change(i, c=i)
            even = Change(a=i)
        seen.append((odd.v, even.v, getattr(odd, 'made', None), getattr(even, 'made', None)))
    out.append(seen)
    out.append([Color(value=1), E('x', code=3).code, E('y', code=4).args])
    return out


for r in run():
    print(r)
