# throw() whose exception constructor resumes the generators it is thrown into.


class ReInit(Exception):
    def __init__(self, *a):
        try:
            next(g)
        except BaseException as e:
            print('inner', type(e).__name__, e)
        super().__init__(*a)


def sub():
    yield 1


def f():
    try:
        yield 3
    finally:
        yield from sub()


g = f()
for i in range(2):
    try:
        print(g.throw(ReInit))
    except BaseException as e:
        print(type(e).__name__, e)

# The constructor finishes the delegate: the exception goes to its delegator.
s = sub()


class Finish(Exception):
    def __init__(self, *a):
        for x in s:
            print('drained', x)
        super().__init__(*a)


def outer():
    try:
        yield from s
    except Finish:
        print('outer caught Finish')
        yield 'after'


o = outer()
print(next(o))
print(o.throw(Finish))
print(list(o))


# The exception's constructor raises: that error is thrown into the
# delegator instead, as _gen_throw's failed throw_here is.
class Refuses(Exception):
    def __init__(self, *a):
        raise KeyError('constructor')


def chain():
    yield from sub()
    yield 'end'


c = chain()
print(next(c))
try:
    c.throw(Refuses)
except KeyError as e:
    print('KeyError', e)
print(list(c))


# Throwing into a finished coroutine through yield from.
class Awaitable:
    def __await__(self):
        yield 'tick'


async def inner():
    await Awaitable()
    return 'done'


co = inner()


def drive():
    try:
        yield from co.__await__()
    except RuntimeError as e:
        print('drive caught', type(e).__name__, e)
        yield 'recovered'


d = drive()
print(next(d))
try:
    co.send(None)
except StopIteration as e:
    print('inner finished', e.value)
try:
    print(d.throw(ValueError('late')))
except Exception as e:
    print(type(e).__name__, e)
