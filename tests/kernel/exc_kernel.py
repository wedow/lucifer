# Exceptions raised through kernel builtins: only user frames show, chaining
# survives, sys.exception() follows generators, except* splits groups.
import sys


class Boom(Exception):
    pass


def show(label, e):
    print(label, type(e).__name__, e.args, type(e.__cause__).__name__,
          type(e.__context__).__name__, e.__suppress_context__)


def frames(e):
    tb = e.__traceback__
    names = []
    while tb is not None:
        names.append(tb.tb_frame.f_code.co_name)
        tb = tb.tb_next
    return names


# sorted with a failing key, a failing comparison and an unorderable pair
def bad_key(x):
    raise Boom('key', x)


try:
    sorted([3, 1], key=bad_key)
except Boom as e:
    show('sorted key', e)
    print(frames(e))


class Unordered:
    def __lt__(self, other):
        raise Boom('lt')


try:
    sorted([Unordered(), Unordered()])
except Boom as e:
    show('sorted lt', e)
    print(frames(e))
try:
    sorted([1, 'a'])
except TypeError as e:
    show('sorted mixed', e)
    print(frames(e))

# dict.__getitem__, __missing__ and hashing errors
class Missing(dict):
    def __missing__(self, key):
        raise Boom('missing', key)


try:
    Missing()['k']
except Boom as e:
    show('missing', e)
    print(frames(e))
try:
    {}['absent']
except KeyError as e:
    show('keyerror', e)
    print(frames(e))
try:
    {}[[]]
except TypeError as e:
    show('unhashable', e)
    print(frames(e))
class BadHash:
    def __hash__(self):
        raise Boom('hash')


try:
    {BadHash(): 1}
except Boom as e:
    show('bad hash', e)
    print(frames(e))

# map, with chaining through the function
def explode(x):
    try:
        1 / x
    except ZeroDivisionError as inner:
        raise Boom('wrapped', x) from inner


try:
    list(map(explode, [1, 0]))
except Boom as e:
    show('map', e)
    print(frames(e))
    print(type(e.__cause__).__name__, frames(e.__cause__))


def reraise(x):
    try:
        abs(x)
    except TypeError:
        raise Boom('during')


try:
    list(map(reraise, ['s']))
except Boom as e:
    show('map context', e)
    print(frames(e), frames(e.__context__))

try:
    list(filter(explode, [0]))
except Boom as e:
    show('filter', e)

class BadIter:
    def __iter__(self):
        return self

    def __next__(self):
        raise Boom('next')


try:
    list(zip([1], BadIter()))
except Boom as e:
    show('zip next', e)
    print(frames(e))
try:
    sum(BadIter())
except Boom as e:
    show('sum', e)
    print(frames(e))
try:
    max(BadIter())
except Boom as e:
    show('max', e)
    print(frames(e))
try:
    getattr(object(), 'nope')
except AttributeError as e:
    show('getattr', e)
    print(frames(e))
try:
    exec(1)
except TypeError as e:
    show('exec', e)

# sys.exception() inside and across generators
def gen():
    print('gen start', sys.exception())
    try:
        yield 1
    except Boom as e:
        print('gen caught', repr(sys.exception()), sys.exception() is e)
        yield 2
    print('gen after', sys.exception())
    yield 3


g = gen()
print(next(g))
try:
    raise Boom('outer')
except Boom:
    print('outer sees', repr(sys.exception()))
    print(g.throw(Boom('thrown')))
    print('outer after throw', repr(sys.exception()))
print('outside', sys.exception())
print(next(g))
print(sys.exception())


def handler_gen():
    try:
        raise Boom('in gen')
    except Boom:
        yield sys.exception()
        yield sys.exception()


try:
    raise KeyError('caller')
except KeyError:
    hg = handler_gen()
    first = next(hg)
    print('first', repr(first), repr(sys.exception()))
    second = next(hg)
    print('second', repr(second), repr(sys.exception()))
print(repr(sys.exception()))

# builtins that run a generator inside an except block
def items():
    try:
        raise Boom('lazy')
    except Boom:
        yield sys.exception().args[0]


try:
    raise KeyError('handling')
except KeyError:
    print(list(items()), repr(sys.exception()))
    print(sorted(map(str, items())), repr(sys.exception()))

# except*
def group():
    raise ExceptionGroup('g', [Boom('a'), ValueError('b'), Boom('c'), KeyError('d')])


try:
    group()
except* Boom as eg:
    print('boom', type(eg).__name__, [str(x) for x in eg.exceptions])
except* (ValueError, KeyError) as eg:
    print('rest', [type(x).__name__ for x in eg.exceptions])

try:
    try:
        group()
    except* Boom:
        print('only boom')
except ExceptionGroup as eg:
    print('leftover', [type(x).__name__ for x in eg.exceptions])

try:
    try:
        raise ExceptionGroup('g', [Boom('a')])
    except* Boom as eg:
        raise RuntimeError('from handler')
except RuntimeError as e:
    print('combined', type(e).__name__, type(e.__context__).__name__)

try:
    list(map(lambda e: (_ for _ in ()).throw(e), [ExceptionGroup('m', [Boom('x')])]))
except* Boom as eg:
    print('map group', type(eg).__name__, frames(eg))

# an uncaught chain through map ends the program
def final(x):
    try:
        {}[x]
    except KeyError as e:
        raise Boom('final', x) from e


print('end')
list(map(final, ['key']))
