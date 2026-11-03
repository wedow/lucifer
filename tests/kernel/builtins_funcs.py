# The builtin functions: results, keyword handling and error texts.
import sys


def t(label, f, *a, **k):
    try:
        r = f(*a, **k)
    except BaseException as e:
        print(label, '!', type(e).__name__, e)
    else:
        print(label, repr(r))


class Num:
    def __init__(self, v):
        self.v = v

    def __abs__(self):
        return 'abs'

    def __round__(self, n=None):
        return ('round', n)

    def __index__(self):
        return self.v

    def __divmod__(self, o):
        return ('divmod', o)

    def __rdivmod__(self, o):
        return ('rdivmod', o)

    def __pow__(self, o, m=None):
        return ('pow', o, m)

    def __rpow__(self, o, m=None):
        return ('rpow', o, m)

    def __add__(self, o):
        return 'add'

    def __radd__(self, o):
        return 'radd'


class Thing:
    x = 1

    def __init__(self):
        self.y = 2


class Slotted:
    def __getattr__(self, name):
        if name == 'magic':
            return 42
        raise AttributeError(name)


# abs, round, divmod, pow
t('abs', abs, -3)
t('abs float', abs, -2.5)
t('abs complex', abs, 3 + 4j)
t('abs obj', abs, Num(1))
t('abs str', abs, 'x')
t('abs none', abs)
t('abs kw', abs, x=1)
t('abs 2', abs, 1, 2)
t('round', round, 2.675, 2)
t('round half', round, 0.5)
t('round half 2', round, 1.5)
t('round int', round, 1234, -2)
t('round obj', round, Num(1), 3)
t('round obj none', round, Num(1), None)
t('round str', round, 'x')
t('round kw', round, 1.25, ndigits=1)
t('round bad', round, 1.5, 1.5)
t('round none', round)
t('round big', round, 10 ** 30, -29)
t('divmod', divmod, 7, -2)
t('divmod float', divmod, 7.5, 2)
t('divmod zero', divmod, 1, 0)
t('divmod obj', divmod, Num(1), 5)
t('divmod robj', divmod, 5, Num(1))
t('divmod str', divmod, 'a', 'b')
t('divmod 1', divmod, 1)
t('pow', pow, 2, 10)
t('pow neg', pow, 2, -2)
t('pow mod', pow, 3, 100, 7)
t('pow mod neg', pow, 3, -1, 7)
t('pow kw', pow, base=2, exp=5)
t('pow kw mod', pow, 3, 100, mod=7)
t('pow obj', pow, Num(1), 3)
t('pow obj mod', pow, Num(1), 3, 5)
t('pow robj', pow, 3, Num(1))
t('pow str', pow, 'a', 2)
t('pow mod str', pow, 2, 3, 'a')
t('pow 0', pow)
t('pow 4', pow, 1, 2, 3, 4)
t('pow float', pow, 2.0, 0.5)

# the number-and-text functions
t('bin', bin, 5)
t('bin neg', bin, -5)
t('bin zero', bin, 0)
t('bin big', bin, 1 << 70)
t('oct', oct, 64)
t('oct neg', oct, -8)
t('hex', hex, 255)
t('hex neg', hex, -255)
t('hex bool', hex, True)
t('hex obj', hex, Num(26))
t('hex float', hex, 1.5)
t('hex str', hex, 'a')
t('chr', chr, 65)
t('chr high', chr, 0x1F600)
t('chr neg', chr, -1)
t('chr big', chr, 0x110000)
t('chr str', chr, 'a')
t('chr obj', chr, Num(97))
t('ord', ord, 'a')
t('ord high', ord, '\U0001F600')
t('ord bytes', ord, b'a')
t('ord bytearray', ord, bytearray(b'z'))
t('ord long', ord, 'ab')
t('ord empty', ord, '')
t('ord bytes long', ord, b'ab')
t('ord int', ord, 1)
t('ascii', ascii, 'h\xe9llo')
t('ascii list', ascii, ['€', 1])
t('repr', repr, 'it\'s')
t('format', format, 3.14159, '.2f')
t('format int', format, 255, 'x')
t('callable', callable, len)
t('callable int', callable, 1)
t('callable class', callable, Thing)
t('callable instance', callable, Thing())
t('callable lambda', callable, lambda: 0)
t('id same', lambda: id(Thing) == id(Thing))
t('id types', lambda: (type(id(1)), id(1) == id(1), id(None) == id(None)))
t('id none', id)
t('hash', hash, (1, 2))
t('hash unhashable', hash, [])

# any, all
t('all', all, [1, 2, 3])
t('all empty', all, [])
t('all false', all, [1, 0, 3])
t('any', any, [0, 0, 1])
t('any empty', any, ())
t('any gen', any, (x > 2 for x in range(5)))
t('all int', all, 3)
t('any 0', any)
t('all lazy', lambda: all(iter([1, 0, 1/0])))

# sum
t('sum', sum, [1, 2, 3])
t('sum start', sum, [1, 2, 3], 10)
t('sum start kw', sum, [1, 2, 3], start=10)
t('sum float', sum, [0.1] * 10)
t('sum float neumaier', sum, [1e100, 1.0, -1e100, 1.0])
t('sum mixed', sum, [1, 2.5, 3])
t('sum float start', sum, [1, 2], 0.5)
t('sum big', sum, [1 << 62, 1 << 62, 1 << 62])
t('sum complex', sum, [1j, 2, 3.5])
t('sum list start', sum, [[1], [2]], [])
t('sum str', sum, ['a', 'b'])
t('sum str start', sum, ['a', 'b'], '')
t('sum bytes', sum, [b'a'], b'')
t('sum bytearray', sum, [bytearray(b'a')], bytearray())
t('sum nan', sum, [float('nan'), 1.0])
t('sum inf', sum, [float('inf'), 1.0])
t('sum inf nan', sum, [float('inf'), float('-inf')])
t('sum bool', sum, [True, True, False])
t('sum range', sum, range(101))
t('sum str items', sum, [1, 'a'])
t('sum none', sum)
t('sum int', sum, 5)
t('sum 3', sum, [1], 2, 3)
t('sum obj', sum, [Num(1), Num(2)])
t('sum float then int', sum, [0.5, 1, 2])
t('sum overflow float', sum, [1.5, 1 << 2000])

# max, min
t('max', max, [3, 1, 2])
t('max args', max, 3, 1, 2)
t('max key', max, ['aa', 'b', 'cccc'], key=len)
t('max default', max, [], default='none')
t('max empty', max, [])
t('max empty args', max)
t('max one arg', max, 5)
t('max default args', max, 1, 2, default=0)
t('max kw', max, [1], foo=1)
t('max first of equal', max, [(1, 'a'), (1, 'b')], key=lambda p: p[0])
t('min', min, [3, 1, 2])
t('min args', min, 3, 1, 2)
t('min key', min, ['aa', 'b', 'cccc'], key=len)
t('min default', min, (), default=7)
t('min empty', min, ())
t('min first of equal', min, [(1, 'a'), (1, 'b')], key=lambda p: p[0])
t('min mixed', min, 1, 'a')
t('min key none', min, [3, 1], key=None)
t('min str', min, 'hello')
t('min dict', min, {3: 'a', 1: 'b'})

# getattr, hasattr, setattr, delattr
th = Thing()
t('getattr', getattr, th, 'x')
t('getattr inst', getattr, th, 'y')
t('getattr missing', getattr, th, 'z')
t('getattr default', getattr, th, 'z', 'dflt')
t('getattr hook', getattr, Slotted(), 'magic')
t('getattr hook default', getattr, Slotted(), 'other', 9)
t('getattr non-str', getattr, th, 1)
t('getattr 1', getattr, th)
t('getattr 4', getattr, th, 'x', 1, 2)
t('getattr kw', getattr, th, name='x')
t('getattr builtin', lambda: getattr('abc', 'upper')())
t('getattr none', getattr, None, '__class__')
t('hasattr', hasattr, th, 'x')
t('hasattr missing', hasattr, th, 'nope')
t('hasattr non-str', hasattr, th, 3)
t('hasattr 1', hasattr, th)
t('setattr', setattr, th, 'z', 5)
t('after setattr', getattr, th, 'z')
t('setattr non-str', setattr, th, 1, 2)
t('setattr builtin', setattr, 1, 'a', 2)
t('setattr 2', setattr, th, 'a')
t('delattr', delattr, th, 'z')
t('after delattr', hasattr, th, 'z')
t('delattr missing', delattr, th, 'z')
t('delattr non-str', delattr, th, 1)

# vars, dir, locals, globals
t('vars', vars, th)
t('vars none', vars, 1)
t('vars class', lambda: (vars(Thing)['x'], 'x' in vars(Thing), 'nope' in vars(Thing)))
t('dir', lambda: [n for n in dir(th) if not n.startswith('__')])
t('dir class', lambda: [n for n in dir(Thing) if not n.startswith('__')])
t('dir object', lambda: '__class__' in dir(object()))
t('dir sorted', lambda: dir(th) == sorted(dir(th)))
t('dir int', lambda: 'bit_length' in dir(1))
t('dir 2', dir, 1, 2)


class WithDir:
    def __dir__(self):
        return ['b', 'a']


t('dir custom', dir, WithDir())


class BadDir:
    def __dir__(self):
        return 1


t('dir bad', dir, BadDir())


def local_view(a, b=2):
    c = a + b
    return locals()


t('locals', local_view, 1)


def local_cell(a):
    def inner():
        return a
    z = 5
    return sorted(locals())


t('locals cell', local_cell, 1)


def local_dir(a):
    b = 1
    return dir()


t('dir noarg', local_dir, 1)
g_value = 99
t('globals', lambda: globals()['g_value'])
t('globals module', lambda: globals()['__name__'])
t('locals module', lambda: locals() is globals())
t('globals 1', globals, 1)
t('locals 1', locals, 1)

# eval and exec of code objects
code = (lambda: None).__code__
ns = {}
t('exec code', exec, code, ns)
t('exec builtins', lambda: '__builtins__' in ns)
t('exec none', exec, 1)
t('exec locals', exec, code, {}, {})
t('eval none', eval, 1)
t('eval no args', eval)
t('exec globals bad', exec, code, 1)

# iteration helpers
t('iter', lambda: list(iter([1, 2])))
t('next', next, iter([1]))
t('next default', next, iter([]), 'd')
t('next exhausted', next, iter([]))
t('next non-iter', next, 1)
t('next 3', next, iter([]), 1, 2)
t('next kw', next, iter([1]), default=1)
t('iter non', iter, 1)
t('iter 3', iter, 1, 2, 3)
t('iter 0', iter)
t('iter kw', iter, [1], sentinel=1)
t('iter sentinel', lambda: list(iter(iter([1, 2, 3, 0, 5]).__next__, 0)))
t('iter sentinel noncallable', iter, 1, 2)
t('iter sentinel type', lambda: type(iter(len, 1)).__name__)
cnt = [0]


def counter():
    cnt[0] += 1
    return cnt[0]


t('iter sentinel counter', lambda: list(iter(counter, 4)))
t('iter sentinel exhausted', lambda: (lambda i: (list(i), list(i)))(iter(counter, 6)))


def stops():
    raise StopIteration


t('iter sentinel stop', lambda: list(iter(stops, 1)))

# enumerate
t('enumerate', lambda: list(enumerate('ab')))
t('enumerate start', lambda: list(enumerate('ab', 5)))
t('enumerate start kw', lambda: list(enumerate('ab', start=-1)))
t('enumerate big', lambda: list(enumerate('ab', 1 << 70)))
t('enumerate bad start', enumerate, 'ab', 'x')
t('enumerate none', enumerate)
t('enumerate noniter', enumerate, 1)
t('enumerate kw', enumerate, 'a', foo=1)
t('enumerate 3', enumerate, 'a', 1, 2)
t('enumerate index', lambda: list(enumerate('ab', Num(3))))
e = enumerate('xy')
t('enumerate iter', lambda: (iter(e) is e, next(e), next(e), next(e, 'end')))
t('enumerate repr', lambda: repr(e)[:20])
t('enumerate type', lambda: (type(e).__name__, type(e).__mro__))

# zip
t('zip', lambda: list(zip('abc', [1, 2, 3])))
t('zip short', lambda: list(zip('abc', [1])))
t('zip none', lambda: list(zip()))
t('zip one', lambda: list(zip('ab')))
t('zip strict', lambda: list(zip('ab', [1, 2], strict=True)))
t('zip strict short', lambda: list(zip('ab', [1], strict=True)))
t('zip strict long', lambda: list(zip('a', [1, 2], strict=True)))
t('zip strict third', lambda: list(zip('a', [1], [1, 2], strict=True)))
t('zip strict third short', lambda: list(zip('ab', 'ab', 'a', strict=True)))
t('zip kw', zip, 'a', foo=1)
t('zip noniter', zip, 1)
t('zip noniter second', zip, 'a', 1)
t('zip strict falsey', lambda: list(zip('a', 'b', strict=0)))
t('zip lazy', lambda: next(zip(iter([1]), iter([2, 3]))))
z = zip('a', 'b')
t('zip iter', lambda: (iter(z) is z, next(z), next(z, 'end')))
t('zip type', lambda: type(z).__name__)

# map and filter
t('map', lambda: list(map(abs, [-1, 2, -3])))
t('map two', lambda: list(map(pow, [2, 3], [3, 2])))
t('map short', lambda: list(map(pow, [2, 3], [3])))
t('map strict', lambda: list(map(pow, [2, 3], [3], strict=True)))
t('map strict ok', lambda: list(map(pow, [2, 3], [3, 1], strict=True)))
t('map none', map)
t('map one', map, abs)
t('map noniter', map, abs, 1)
t('map kw', map, abs, [1], foo=1)
t('map call error', lambda: list(map(abs, ['x'])))
t('map lazy', lambda: next(map(abs, iter([-1, 'x']))))
t('map type', lambda: type(map(abs, [])).__name__)
t('filter', lambda: list(filter(None, [0, 1, '', 'a', None])))
t('filter fn', lambda: list(filter(lambda x: x > 1, [1, 2, 3])))
t('filter 1', filter, None)
t('filter noniter', filter, None, 1)
t('filter kw', filter, None, [], foo=1)
t('filter fn error', lambda: list(filter(abs, ['x'])))
t('filter type', lambda: type(filter(None, [])).__name__)

# reversed
t('reversed list', lambda: list(reversed([1, 2, 3])))
t('reversed str', lambda: list(reversed('abc')))
t('reversed tuple', lambda: list(reversed((1, 2))))
t('reversed range', lambda: list(reversed(range(3))))
t('reversed dict', lambda: list(reversed({1: 'a', 2: 'b'})))
t('reversed set', reversed, {1, 2})
t('reversed int', reversed, 1)
t('reversed none', reversed)
t('reversed kw', reversed, [], seq=1)
t('reversed type list', lambda: type(reversed([1])).__name__)
t('reversed type str', lambda: type(reversed('ab')).__name__)


class Seq:
    def __len__(self):
        return 3

    def __getitem__(self, i):
        if not 0 <= i < 3:
            raise IndexError(i)
        return i * 10


class Rev:
    def __reversed__(self):
        return iter('rev')


class NoRev:
    __reversed__ = None


t('reversed seq', lambda: list(reversed(Seq())))
t('reversed seq type', lambda: type(reversed(Seq())).__name__)
t('reversed seq hint', lambda: reversed(Seq()).__length_hint__())
t('reversed custom', lambda: list(reversed(Rev())))
t('reversed none slot', reversed, NoRev())
r = reversed(Seq())
t('reversed iter', lambda: (iter(r) is r, next(r), next(r), next(r), next(r, 'end')))


class Shrinks:
    def __init__(self):
        self.n = 3

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        if i >= self.n:
            raise IndexError
        return i


s = Shrinks()
r = reversed(s)
s.n = 1
t('reversed shrinks', lambda: list(r))


class SubMap(map):
    pass


class SubEnum(enumerate):
    extra = 1


t('map subclass', lambda: list(SubMap(abs, [-1, 2])))
t('enumerate subclass', lambda: (list(SubEnum('ab')), SubEnum.extra))
t('map subclass attr', lambda: (setattr(SubMap(abs, []), 'a', 1)))

# sorted and other earlier builtins still compose with the new ones
t('sorted key', sorted, ['bb', 'a', 'ccc'], key=len, reverse=True)
t('sorted map', lambda: sorted(map(str, [3, 1, 2])))
t('isinstance', isinstance, 1, (str, int))
t('issubclass', issubclass, bool, int)
t('len', len, 'abc')
t('len 0', len)
t('NotImplemented', lambda: (NotImplemented, Ellipsis, ...))
t('__import__', lambda: callable(__import__))
t('__debug__', lambda: __debug__)
t('breakpoint', lambda: callable(breakpoint))
t('input', lambda: callable(input))
t('aiter', lambda: callable(aiter))
t('compile', lambda: callable(compile))
