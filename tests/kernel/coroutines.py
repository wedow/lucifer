# Coroutines, async for/with and async generators, driven by a hand-written
# event loop (there is no asyncio in the kernel).
import sys


def show(label, f):
    try:
        print(label, f())
    except BaseException as e:
        print(label, type(e).__name__, e)


class Suspend:
    """An awaitable that yields one request to the loop."""

    def __init__(self, request):
        self.request = request

    def __await__(self):
        reply = yield self.request
        return reply


def run(coro):
    """Drive coro to completion, answering each request with its double."""
    reply = None
    steps = 0
    while True:
        try:
            request = coro.send(reply)
        except StopIteration as e:
            return steps, e.value
        steps += 1
        reply = request * 2 if type(request) is int else request


async def add(a, b):
    x = await Suspend(a)
    y = await Suspend(b)
    return x + y


async def main():
    s = await add(1, 2)
    t = await add(s, 10)
    return t


print('run', run(main()))
c = add(1, 2)
print(type(c).__name__, c.__name__, c.__qualname__, c.cr_running, c.cr_suspended)
print('cr_code', c.cr_code is add.__code__, c.cr_frame is not None, c.cr_await, c.cr_origin)
print('first', c.send(None))
print('cr_await', type(c.cr_await).__name__, c.cr_suspended)
print('second', c.send(5))
try:
    c.send(6)
except StopIteration as e:
    print('result', e.value)
print('done', c.cr_frame, c.cr_await, c.cr_suspended)
show('reuse', lambda: c.send(None))
show('reuse throw', lambda: c.throw(ValueError('x')))
print('close done', c.close())
unawaited = add(1, 2)
show('not iterable', lambda: iter(unawaited))
show('nonNone first', lambda: unawaited.send(1))
unawaited.close()
cl = add(1, 2)
print('close unstarted', cl.close())


# await on bad awaitables
class NoAwait:
    pass


class AwaitReturnsInt:
    def __await__(self):
        return 5


async def awaits(x):
    return await x


show('await int', lambda: run(awaits(5)))
show('await object', lambda: run(awaits(NoAwait())))
show('await bad iter', lambda: run(awaits(AwaitReturnsInt())))


async def nothing():
    return 'nothing'


async def awaits_twice():
    c = nothing()
    await c
    await c


show('await coroutine twice', lambda: run(awaits_twice()))


# __await__ of a coroutine: the coroutine wrapper
w = nothing().__await__()
print('wrapper', type(w).__name__, iter(w) is w)
try:
    next(w)
except StopIteration as e:
    print('wrapper result', e.value)


# throw and close through await
async def guarded():
    try:
        await Suspend(1)
    except ValueError as e:
        print('coroutine caught', e)
        return 'recovered'
    finally:
        print('guarded finally')


g = guarded()
g.send(None)
try:
    g.throw(ValueError('boom'))
except StopIteration as e:
    print('throw result', e.value)
g2 = guarded()
g2.send(None)
g2.close()
print('closed', g2.cr_frame)


async def leak():
    raise StopIteration('leaked')


show('pep479 coroutine', lambda: run(leak()))


# async with
class Manager:
    def __init__(self, name, suppress=False):
        self.name = name
        self.suppress = suppress

    async def __aenter__(self):
        await Suspend(0)
        print('aenter', self.name)
        return self

    async def __aexit__(self, kind, value, tb):
        await Suspend(0)
        print('aexit', self.name, kind.__name__ if kind else None)
        return self.suppress


async def with_body():
    async with Manager('a') as m:
        print('inside', m.name)
    async with Manager('b', True):
        raise KeyError('suppressed')
    async with Manager('c'):
        raise KeyError('escapes')


try:
    run(with_body())
except KeyError as e:
    print('escaped', e)


async def no_aenter():
    async with 5:
        pass


show('async with bad', lambda: run(no_aenter()))


# async iterators and async for
class Ticker:
    def __init__(self, n):
        self.i = 0
        self.n = n

    def __aiter__(self):
        return self

    async def __anext__(self):
        await Suspend(self.i)
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return self.i


async def consume():
    seen = []
    async for x in Ticker(3):
        seen.append(x)
    else:
        seen.append('else')
    return seen


print('async for', run(consume()))


async def bad_async_for():
    async for x in [1, 2]:
        pass


show('async for list', lambda: run(bad_async_for()))


# async generators
async def agen(n):
    for i in range(n):
        got = await Suspend(i)
        yield got
    print('agen exhausted')


async def use_agen():
    out = []
    async for v in agen(3):
        out.append(v)
    return out


print('agen loop', run(use_agen()))
ag = agen(2)
print(type(ag).__name__, ag.__name__, ag.ag_running, ag.ag_suspended, ag.ag_await,
      ag.ag_code is agen.__code__, ag.ag_frame is not None)
print('aiter self', ag.__aiter__() is ag)
nx = ag.__anext__()
print(type(nx).__name__)
print('asend step', nx.send(None))
print('ag_await', type(ag.ag_await).__name__, ag.ag_running)
try:
    nx.send(7)
except StopIteration as e:
    print('yielded via StopIteration', e.value)
show('reuse anext', lambda: nx.send(None))


async def drive_asend():
    a = agen(3)
    first = await a.asend(None)
    second = await a.asend('ignored')
    return first, second


print('asend', run(drive_asend()))


async def acleanup():
    try:
        yield 1
        yield 2
    finally:
        await Suspend(0)
        print('acleanup finally')


async def drive_aclose():
    a = acleanup()
    print('got', await a.__anext__())
    r = await a.aclose()
    print('aclose result', r, a.ag_frame)
    try:
        await a.__anext__()
    except StopAsyncIteration:
        print('closed agen stops')
    return 'ok'


print('aclose', run(drive_aclose()))


async def acatch():
    while True:
        try:
            yield 'ready'
        except ValueError as e:
            yield 'caught ' + str(e)


async def drive_athrow():
    a = acatch()
    await a.__anext__()
    r = await a.athrow(ValueError('v'))
    return r


print('athrow', run(drive_athrow()))


async def ignores_exit():
    try:
        yield 1
    except GeneratorExit:
        yield 2


async def drive_ignore():
    a = ignores_exit()
    await a.__anext__()
    await a.aclose()


show('ignore aclose', lambda: run(drive_ignore()))


async def agen_leak():
    yield 1
    raise StopAsyncIteration


async def drive_leak():
    async for x in agen_leak():
        pass


show('agen raises StopAsyncIteration', lambda: run(drive_leak()))


async def agen_returns():
    yield 1
    return


async def comprehension():
    return [x async for x in agen(3)], [await Suspend(i) for i in range(2)]


print('async comprehension', run(comprehension()))


# await inside a generator-style chain: 50 coroutines deep
async def nested(n):
    if n == 0:
        return await Suspend(1)
    return await nested(n - 1) + 1


print('nested', run(nested(50)))


# the task's async generator hooks
def hooks():
    seen = []

    def firstiter(agen):
        seen.append(agen.__name__)

    sys.set_asyncgen_hooks(firstiter=firstiter)

    async def ag():
        yield 1

    a = ag()
    nx = a.__anext__()
    try:
        nx.send(None)
    except StopIteration as e:
        print('hook value', e.value)
    sys.set_asyncgen_hooks(firstiter=None)
    print('hook seen', seen)
    try:
        sys.set_asyncgen_hooks(firstiter=5)
    except TypeError as e:
        print(e)


hooks()
