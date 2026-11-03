# match statements: every pattern kind, and __init__ returning a value.
import collections
from dataclasses import dataclass


@dataclass
class Point:
    x: int
    y: int


class Pos:
    __match_args__ = ('a', 'b')

    def __init__(self, a, b):
        self.a, self.b = a, b


def classify(v):
    match v:
        case None:
            return 'none'
        case True | False:
            return 'bool'
        case int(n) if n > 100:
            return 'big %d' % n
        case int() | float():
            return 'number'
        case str() as s if s.startswith('a'):
            return 'a-string ' + s
        case 'lit':
            return 'literal'
        case str(s):
            return 'string ' + s
        case bytes(b):
            return 'bytes'
        case [] | ():
            return 'empty sequence'
        case [1, 2, *rest]:
            return 'one two %r' % (rest,)
        case [x, y] | (x, y, _):
            return 'pair %r %r' % (x, y)
        case [*_]:
            return 'sequence'
        case {'k': 1, **rest}:
            return 'k=1 rest %r' % (rest,)
        case {'a': a, 'b': b}:
            return 'ab %r %r' % (a, b)
        case {}:
            return 'mapping'
        case Point(x=0, y=yy):
            return 'on y axis %d' % yy
        case Point(x, y):
            return 'point %d %d' % (x, y)
        case Pos(1, b):
            return 'pos1 %r' % (b,)
        case Pos(a=2):
            return 'pos2'
        case collections.deque():
            return 'deque'
        case _:
            return 'other'


for value in (None, True, 5, 500, 1.5, 'abc', 'lit', 'zzz', b'x', [], (), [1, 2], [1, 2, 3, 4], [5, 6], (7, 8, 9), [1],
              {'k': 1, 'z': 2}, {'a': 1, 'b': 2}, {}, {'q': 1}, Point(0, 3), Point(2, 3), Pos(1, 9), Pos(2, 0), Pos(3, 3),
              collections.deque([1]), collections.OrderedDict(a=1, b=2), range(3), memoryview(b'ab'), object(), 'a', 'aa'):
    print(type(value).__name__, classify(value))


nonclass = 5


def errors():
    try:
        match 1:
            case nonclass(): pass
    except TypeError as e:
        print(e)
    try:
        match Pos(1, 2):
            case Pos(1, 2, 3): pass
    except TypeError as e:
        print(e)
    try:
        match 5:
            case int(1, 2): pass
    except TypeError as e:
        print(e)


errors()


class BadInit:
    def __init__(self):
        return 5


try:
    BadInit()
except TypeError as e:
    print(e)
