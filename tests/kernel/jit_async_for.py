# Hot `async for` and `async with` compiled in the frame that runs them:
# over an async generator, over a class with __aiter__ and __anext__, over
# an async generator nested in another (a tree), breaking out of the loop
# (the generator closed by aclose), athrow into a suspended async
# generator, an async generator that raises, a context manager that
# suppresses and one that does not, and the loop's error paths (an object
# without __aiter__, __aiter__ returning one without __anext__). Run both
# by driving send() and under asyncio.run, whose shutdown finalizes the
# async generators left open.
import asyncio
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300


async def agen(n, log):
    try:
        for i in range(n):
            got = yield i
            if got is not None:
                log.append(('asend', got))
            if i % 2:
                await asyncio.sleep(0)
    except KeyError as e:
        log.append(('agen caught', e.args))
        yield 'after throw'
    finally:
        log.append(('agen finally', n))


class Counter:
    def __init__(self, n):
        self.i, self.n = 0, n

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        await asyncio.sleep(0)
        return self.i


class Tree:
    def __init__(self, left, value, right):
        self.left, self.value, self.right = left, value, right

    async def __aiter__(self):
        if self.left:
            async for i in self.left:
                yield i
        yield self.value
        if self.right:
            async for i in self.right:
                yield i


def tree(lo, hi):
    if lo >= hi:
        return None
    mid = (lo + hi) // 2
    return Tree(tree(lo, mid), mid, tree(mid + 1, hi))


async def consume(n):
    log = []
    total = 0
    async for v in agen(n, log):
        total += v
    async for v in Counter(n):
        total += v
    async for v in tree(0, n):
        total += v
    return total, log


async def breaks(n):
    log = []
    g = agen(n, log)
    async for v in g:
        if v == 2:
            break
    await g.aclose()
    return log


async def throws(n):
    log = []
    g = agen(n, log)
    out = [await g.asend(None), await g.asend('x')]
    out.append(await g.athrow(KeyError('k')))
    try:
        await g.asend(None)
    except StopAsyncIteration:
        out.append('stopped')
    return out, log


async def raising():
    yield 1
    raise ValueError('agen raised')


async def raises():
    out = []
    try:
        async for v in raising():
            out.append(v)
    except ValueError as e:
        out.append(e.args)
    for bad in (object(), Bad()):
        try:
            async for v in bad:
                pass
        except TypeError as e:
            out.append(str(e))
    return out


class Bad:
    def __aiter__(self):
        return 42


class Manager:
    def __init__(self, log, suppress):
        self.log, self.suppress = log, suppress

    async def __aenter__(self):
        await asyncio.sleep(0)
        self.log.append('enter')
        return self

    async def __aexit__(self, typ, val, tb):
        await asyncio.sleep(0)
        self.log.append(('exit', typ.__name__ if typ else None))
        return self.suppress


async def managed(fail, suppress):
    log = []
    try:
        async with Manager(log, suppress) as m:
            log.append(m is not None)
            if fail:
                raise LookupError('in with')
    except LookupError as e:
        log.append(('escaped', e.args))
    return log


def drive(coro):
    try:
        while True:
            coro.send(None)
    except StopIteration as e:
        return e.value


async def left_open(log):
    g = agen(5, log)
    async for v in g:
        if v == 1:
            break                   # not closed: asyncio.run's shutdown finalizes it


async def main(n):
    out = []
    for _ in range(n // 30 + 1):
        out.append(await consume(9))
        out.append(await breaks(6))
        out.append(await throws(6))
        out.append(await raises())
        for fail in (False, True):
            for suppress in (False, True):
                out.append(await managed(fail, suppress))
    log = []
    await left_open(log)
    out.append(log)
    return out[-12:]


results = []
for _ in range(N // 30 + 1):
    results.append(drive(consume(7)))
    results.append(drive(managed(True, True)))
print(results[-2:])
print(asyncio.run(main(N)))
