"""Calling a class whose __new__ is object's and whose __init__ is a Python
function runs in the eval loop; every other class, and every error, takes
type.__call__ with the same results, messages and tracebacks."""

import abc
import dataclasses
import sys
import traceback


class Plain:
    pass


class WithInit:
    def __init__(self, a, b=2, *rest, k=None, **kw):
        self.a, self.b, self.rest, self.k, self.kw = a, b, rest, k, kw


class Child(WithInit):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.child = True


class Slots:
    __slots__ = ('x',)

    def __init__(self, x):
        self.x = x


def attempt(label, make):
    for _ in range(2):
        try:
            r = make()
            print(label, type(r).__name__, getattr(r, '__dict__', None))
        except Exception as ex:
            print(label, type(ex).__name__, ex)


attempt(1, lambda: Plain())
attempt(2, lambda: Plain(1))
attempt(3, lambda: Plain(a=1))
attempt(4, lambda: WithInit(1, 2, 3, 4, k=5, z=6))
attempt(5, lambda: WithInit())
attempt(6, lambda: WithInit(1, b=3, b2=4))
attempt(7, lambda: WithInit(1, 2, a=3))
attempt(8, lambda: Child(1, k=2))
attempt(9, lambda: Slots(3).x)
attempt(10, lambda: Slots())


class Bad:
    def __init__(self):
        return 1


class Gen:
    def __init__(self):
        yield 1


class Boom:
    def __init__(self):
        raise ValueError('boom')


attempt(11, lambda: Bad())
attempt(12, lambda: Gen())
attempt(13, lambda: Boom())


class Abstract(abc.ABC):
    @abc.abstractmethod
    def f(self):
        pass

    def __init__(self):
        pass


class Concrete(Abstract):
    def f(self):
        return 1


attempt(14, lambda: Abstract())
attempt(15, lambda: Concrete().f())


class New:
    def __new__(cls, *args):
        return ('new', args)

    def __init__(self, *args):
        print('not called')


class NewSame:
    def __new__(cls, *args):
        return super().__new__(cls)

    def __init__(self, *args):
        self.args = args


attempt(16, lambda: New(1))
attempt(17, lambda: NewSame(1, 2).args)


class Meta(type):
    def __call__(cls, *args):
        return 'meta call'


class WithMeta(metaclass=Meta):
    def __init__(self):
        print('not called')


class Callable_:
    def __call__(self, obj, *args):
        print('callable init')


class CI:
    __init__ = Callable_()


attempt(18, lambda: WithMeta())
attempt(19, lambda: CI())
attempt(20, lambda: sys.getrecursionlimit() > 0 and Exception('x').args)


class Ctx:
    def __init__(self):
        f = sys._getframe(1)
        print('caller', f.f_code.co_name, f.f_lineno - f.f_code.co_firstlineno)


def maker():
    return Ctx()


maker()


class Tb:
    def __init__(self, n):
        if n:
            Tb(n - 1)
        else:
            raise KeyError('deep')


try:
    Tb(2)
except KeyError:
    print(21, ''.join(line.split(', in ')[-1] if 'File' in line else line.strip() + '\n'
                      for line in traceback.format_exc().splitlines(True)))


@dataclasses.dataclass
class DC:
    x: int
    y: int = 2


print(22, DC(1), DC(1, 3) == DC(1, 3))


class E(Exception):
    def __init__(self, msg):
        super().__init__(msg)
        self.msg = msg


print(23, E('m').args, E('m').msg)
try:
    E()
except TypeError as ex:
    print(24, ex)


class Del:
    def __init__(self):
        print('init')

    def __del__(self):
        print('del')


Del()
print(25)


class Deep:
    def __init__(self, n):
        self.child = Deep(n - 1) if n else None


d = Deep(50)
n = 0
while d:
    d, n = d.child, n + 1
print(26, n)


def recurse():
    class R:
        def __init__(self):
            R()
    R()


try:
    recurse()
except RecursionError:
    print(27, 'recursion error')


class Late:
    pass


Late.__init__ = lambda self, v: setattr(self, 'v', v)
print(28, Late(5).v)
del Late.__init__
attempt(29, lambda: Late(5))
Late.__new__ = lambda cls, *a: object.__new__(cls)
attempt(30, lambda: Late())
