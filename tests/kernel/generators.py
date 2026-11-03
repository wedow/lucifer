# Generators: the protocol, send/throw/close, yield from, PEP 479 and the
# gi_* attributes.


def show(label, f):
    try:
        print(label, f())
    except BaseException as e:
        print(label, type(e).__name__, e)


def count(n):
    i = 0
    while i < n:
        yield i
        i += 1
    return 'end'


g = count(3)
print(type(g).__name__, g.__name__, g.__qualname__)
print('iter is self', iter(g) is g)
print('list', list(count(4)))
print('for', [x * 2 for x in count(3)])
print(next(g), next(g), next(g))
try:
    next(g)
except StopIteration as e:
    print('stop', e.value, e.args)
show('again', lambda: next(g))
print('default', next(g, 'dflt'))
print('tuple', tuple(count(2)))
a, b = count(2)
print('unpack', a, b)
print('in', 2 in count(5), 9 in count(5))


# send
def echo():
    received = []
    try:
        while True:
            v = yield len(received)
            received.append(v)
    finally:
        print('echo finally', received)


e = echo()
print('start', e.send(None))
print('send', e.send('a'), e.send('b'))
e.close()
print('closed again', e.close())
show('send to closed', lambda: e.send(1))
show('nonNone first', lambda: echo().send(1))


# return values become StopIteration.value
def ret(v):
    yield 1
    return v


for v in [None, 5, (1, 2)]:
    r = ret(v)
    next(r)
    try:
        next(r)
    except StopIteration as e:
        print('ret value', e.value, e.args)


# throw
def catcher():
    while True:
        try:
            yield 'ready'
        except ValueError as e:
            print('caught', e)
            yield 'handled'


c = catcher()
next(c)
print('throw', c.throw(ValueError('one')))
print('next', next(c))
print('throw type', c.throw(ValueError))
show('throw uncaught', lambda: c.throw(KeyError('k')))
print('after uncaught', c.gi_frame, c.gi_running, c.gi_suspended)
show('throw bad', lambda: count(1).throw(1))
fresh = count(1)
show('throw fresh', lambda: fresh.throw(ValueError('fresh')))
print('fresh after', fresh.gi_frame)


def returns_on_throw():
    try:
        yield 1
    except ValueError:
        return 'got it'


r = returns_on_throw()
next(r)
try:
    r.throw(ValueError)
except StopIteration as e:
    print('throw returned', e.value)


# close and GeneratorExit
def closer():
    try:
        yield 1
        yield 2
    except GeneratorExit:
        print('GeneratorExit seen')
        raise
    finally:
        print('closer finally')


cl = closer()
next(cl)
print('close', cl.close())
print('closed', cl.gi_frame, cl.gi_suspended)


def ignores():
    try:
        yield 1
    except GeneratorExit:
        yield 'ignored'


ig = ignores()
next(ig)
show('ignore exit', ig.close)


def close_returns():
    try:
        yield 1
    except GeneratorExit:
        return 'closing value'


cr = close_returns()
next(cr)
print('close value', cr.close())
print('close unstarted', count(2).close())


def close_raises():
    try:
        yield 1
    except GeneratorExit:
        raise KeyError('during close')


cx = close_raises()
next(cx)
show('close raises', cx.close)


# PEP 479
def leaks():
    yield 1
    raise StopIteration('leaked')


lk = leaks()
next(lk)
try:
    next(lk)
except RuntimeError as e:
    print('pep479', e, type(e.__cause__).__name__, e.__cause__, e.__context__ is e.__cause__)


def inner_stop():
    next(iter([]))
    yield 1


show('pep479 next', lambda: list(inner_stop()))


# yield from
def inner():
    x = yield 'i1'
    print('inner got', x)
    yield 'i2'
    return 'inner result'


def outer():
    r = yield from inner()
    print('outer got', r)
    r2 = yield from [10, 20]
    print('list result', r2)
    yield 'o'
    return 'outer result'


o = outer()
print(next(o))
print(o.gi_yieldfrom.__name__, o.gi_yieldfrom.gi_running)
print(o.send('hello'))
print(next(o), next(o))
print(type(o.gi_yieldfrom).__name__)
print(next(o))
print(o.gi_yieldfrom)
try:
    next(o)
except StopIteration as e:
    print('outer stop', e.value)


def deleg_throw():
    try:
        yield from catcher()
    except KeyError as e:
        print('outer caught', e)
        yield 'outer handled'


dt = deleg_throw()
print(next(dt))
print('deleg throw', dt.throw(ValueError('to inner')))
print(next(dt))
print('deleg throw up', dt.throw(KeyError('up')))


def deleg_close():
    try:
        yield from closer()
    finally:
        print('outer finally')


dc = deleg_close()
next(dc)
dc.close()
print('deleg closed', dc.gi_frame)


class Sub:
    """An iterator with send, throw and close, delegated to by yield from."""

    def __init__(self):
        self.n = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.n += 1
        if self.n > 3:
            raise StopIteration('sub done')
        return self.n

    def send(self, v):
        print('Sub.send', v)
        return self.__next__()

    def throw(self, *args):
        print('Sub.throw', type(args[0]).__name__)
        raise StopIteration('thrown done')

    def close(self):
        print('Sub.close')


def uses_sub():
    r = yield from Sub()
    print('sub result', r)
    yield 'after sub'


us = uses_sub()
print(next(us), us.send('x'), next(us))
print(next(us))
us2 = uses_sub()
next(us2)
print('sub throw', us2.throw(ValueError('v')))
us3 = uses_sub()
next(us3)
us3.close()


def yf_noniter():
    yield from 5


show('yield from noniter', lambda: next(yf_noniter()))


# gi_running, gi_suspended, gi_code, gi_frame
def introspect():
    print('running inside', me.gi_running, me.gi_suspended)
    show('reentry', lambda: next(me))
    yield 1


me = introspect()
print('before', me.gi_running, me.gi_suspended, me.gi_code is introspect.__code__,
      me.gi_frame is not None, me.gi_yieldfrom)
print('frame code', me.gi_frame.f_code is me.gi_code, me.gi_frame.f_lineno)
next(me)
print('suspended', me.gi_running, me.gi_suspended, me.gi_frame.f_lineno)
for _ in me:
    pass
print('finished', me.gi_running, me.gi_suspended, me.gi_frame)
print('code after', me.gi_code.co_name)
me.__name__ = 'renamed'
me.__qualname__ = 'q.renamed'
print(me.__name__, me.__qualname__)


def set_name_bad():
    me.__name__ = 5


show('bad name', set_name_bad)


# exception state is per generator
def exc_state():
    try:
        raise KeyError('in gen')
    except KeyError:
        yield 'in handler'
        try:
            raise
        except KeyError as e:
            print('reraised in gen', e)
    yield 'done'


es = exc_state()
print(next(es))
try:
    raise ValueError('outside')
except ValueError:
    print(next(es))


# generator expressions and closures
def make(n):
    return (i * n for i in range(4))


print('genexpr', list(make(3)))
ge = (x for x in [1, 2])
print(ge.__name__, ge.__qualname__)
show('genexpr first iter', lambda: list(x for x in 5))


# a generator that recurses through yield from
def tree(n):
    if n:
        yield from tree(n - 1)
        yield n
        yield from tree(n - 1)


print('tree', list(tree(4)))


# throw and close through a 100-deep chain; dropping a suspended generator closes it
def chain(n):
    if n == 0:
        try:
            yield 'bottom'
        except KeyError as e:
            yield 'caught ' + str(e)
        finally:
            print('bottom finally')
        return
    yield from chain(n - 1)


g = chain(100)
print(next(g))
print(g.throw(KeyError('deep')))
g.close()
print('closed', g.gi_frame)


def dropped():
    try:
        yield 1
        yield 2
    finally:
        print('dropped finally')


for x in dropped():
    print('first', x)
    break
print('after break')
d = dropped()
next(d)
del d
print('after del')
