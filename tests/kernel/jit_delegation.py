# Delegation compiled in the frame that delegates: hot `yield from` and
# `await` whose SEND runs in compiled code, the receiver resumed under the
# delegating frame's unit. Sub-iterators of every kind (a generator, a
# list iterator, an object with send, throw and close, one without them),
# return values through StopIteration, throw() and close() at each
# suspension of a delegating frame compiled code suspended, await chains
# several frames deep, a cancelled task, exceptions crossing awaits, the
# exception a frame suspended inside a handler holds (await in except,
# in finally, in __aexit__; a generator yielding in except, resumed, then
# re-raising), a guard the delegating frame relied on broken while it is
# suspended and while the receiver runs nested under its unit, a receiver
# writing the delegating frame's local through f_locals, frame walkers
# while a receiver runs, a generator collected
# while suspended inside `yield from`, and a 5000-deep await chain.
import asyncio
import gc
import inspect
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300


def leaf(n):
    total = 0
    for i in range(n):
        got = yield i
        if got is not None:
            total += got
    return total


class Sender:
    """An iterator with send, throw and close."""

    def __init__(self, n, log):
        self.i, self.n, self.log = 0, n, log

    def __iter__(self):
        return self

    def __next__(self):
        return self.send(None)

    def send(self, v):
        if self.i >= self.n:
            raise StopIteration(('sender', self.i))
        self.i += 1
        if v is not None:
            self.log.append(('sent', v))
        return self.i * 10

    def throw(self, typ, val=None, tb=None):
        exc = typ if isinstance(typ, BaseException) else typ(val)
        self.log.append(('throw', type(exc).__name__))
        if isinstance(exc, KeyError):
            return -1
        raise exc

    def close(self):
        self.log.append(('close', self.i))


class Plain:
    """An iterator without send, throw or close."""

    def __init__(self, n):
        self.i, self.n = 0, n

    def __iter__(self):
        return self

    def __next__(self):
        if self.i >= self.n:
            raise StopIteration
        self.i += 1
        return -self.i


def delegate(kind, n, log):
    sub = leaf(n) if kind == 'gen' else Sender(n, log) if kind == 'sender' else Plain(n) if kind == 'plain' \
        else iter(list(range(n)))
    try:
        r = yield from sub
    except Exception as e:
        log.append(('delegate caught', type(e).__name__))
        r = 'caught'
    finally:
        log.append(('delegate finally', kind))
    yield ('returned', r)
    return ('done', kind)


def drive(kind, n):
    log = []
    g = delegate(kind, n, log)
    out = [next(g)]
    for i in range(n + 2):
        try:
            out.append(g.send(i if kind != 'plain' else None))
        except StopIteration as e:
            out.append(('stop', e.value))
            break
    return out, log


def interrupt(kind, n, at, how):
    """throw() or close() at suspension `at` of a delegating generator."""
    log = []
    g = delegate(kind, n, log)
    out = [next(g)]
    for _ in range(at):
        out.append(next(g))
    yf = g.gi_yieldfrom
    out.append((type(yf).__name__ if yf is not None else None, inspect.getgeneratorstate(g)))
    try:
        if how == 'close':
            out.append(('close', g.close()))
        else:
            out.append(('throw', g.throw(how('x'))))
            out.append(('after', next(g, 'end')))
    except BaseException as e:
        out.append(('raised', type(e).__name__, str(e)))
    out.append(inspect.getgeneratorstate(g))
    return out, log


def tree_iter(t):
    if t is None:
        return
    left, value, right = t
    yield from tree_iter(left)
    yield value
    yield from tree_iter(right)


def tree(lo, hi):
    if lo >= hi:
        return None
    mid = (lo + hi) // 2
    return (tree(lo, mid), mid, tree(mid + 1, hi))


def delegations(n):
    out = []
    t = tree(0, 40)
    total = 0
    for _ in range(n):
        for v in tree_iter(t):
            total += v
    out.append(total)
    for kind in ('gen', 'sender', 'plain', 'list'):
        out.append((kind, drive(kind, 3)))
    for kind in ('gen', 'sender', 'plain'):
        for at in (0, 1, 2):
            for how in ('close', KeyError, ValueError, GeneratorExit):
                out.append((kind, at, getattr(how, '__name__', how), interrupt(kind, 3, at, how)))
    return out


# --- await ---

class Step:
    def __await__(self):
        got = yield 'step'
        return got


class Bad:
    def __await__(self):
        return 42


async def chain(depth, fail=None):
    if depth == 0:
        got = await Step()
        if fail is not None:
            raise fail(got)
        return got
    try:
        r = await chain(depth - 1, fail)
    except KeyError as e:
        r = ('caught at', depth, e.args)
    finally:
        pass
    return (r, depth) if depth % 3 == 0 else r


def run_coro(c, sends):
    out = []
    try:
        out.append(c.send(None))
        for s in sends:
            out.append(c.send(s))
    except StopIteration as e:
        out.append(('return', e.value))
    except Exception as e:
        out.append(('raised', type(e).__name__, e.args))
    return out


async def no_suspend(n):
    if n <= 1:
        return n
    return await no_suspend(n - 1) + await no_suspend(n - 2)


async def awaits_twice(c):
    first = await c
    return first


def plain_gen_over_coro(c):
    yield from c


async def awaits_bad():
    return await Bad()


def awaiting(n):
    out = []
    for i in range(n):
        out.append(run_coro(chain(6), [i]))
    out.append(run_coro(chain(6, KeyError), ['k']))
    out.append(run_coro(chain(6, ValueError), ['v']))
    for i in range(n):
        out.append(run_coro(no_suspend(12), []))
    c = chain(2)
    c.send(None)
    out.append(run_coro(awaits_twice(c), []))
    out.append(run_coro(awaits_bad(), []))
    inner = chain(1)
    try:
        next(plain_gen_over_coro(inner))
    except TypeError as e:
        out.append(('TypeError', str(e)))
    inner.close()
    c.close()
    return out


# --- the exception a suspended frame holds ---

async def in_except():
    try:
        raise ValueError
    except ValueError:
        await Step()
        seen = sys.exception()
        return repr(seen)


async def in_except_reraise():
    try:
        raise ValueError
    except ValueError:
        await Step()
        raise


async def in_finally(log):
    try:
        raise LookupError('pending')
    finally:
        await Step()
        log.append(('finally', repr(sys.exception())))


async def catches_finally():
    log = []
    try:
        await in_finally(log)
    except LookupError as e:
        log.append(('after', e.args))
    return log


class Manager:
    def __init__(self, log):
        self.log = log

    async def __aenter__(self):
        await Step()
        return self

    async def __aexit__(self, typ, val, tb):
        await Step()
        self.log.append(('aexit', typ.__name__ if typ else None, repr(sys.exception())))
        return typ is KeyError


async def with_manager(fail):
    log = []
    async with Manager(log):
        await Step()
        if fail is not None:
            raise fail
    return log


def yields_in_except():
    try:
        raise KeyError
    except KeyError:
        yield repr(sys.exception())
        yield repr(sys.exception())
        raise


def exception_state(n):
    out = []
    for _ in range(n):
        out.append(run_coro(in_except(), [None]))
        out.append(run_coro(in_except_reraise(), [None]))
        out.append(run_coro(catches_finally(), [None]))
    for fail in (None, KeyError, ValueError):
        out.append(run_coro(with_manager(fail), [None, None, None, None]))
    for _ in range(n):
        g = yields_in_except()
        out.append(next(g))
        out.append(repr(sys.exception()))
        out.append(next(g))
        try:
            next(g)
        except KeyError as e:
            out.append(('reraised', e.args))
    return out


# --- asyncio ---

async def sleeper(log):
    try:
        await asyncio.sleep(10)
    except asyncio.CancelledError:
        log.append('cancelled')
        raise


async def gathered(i):
    await asyncio.sleep(0)
    if i == 3:
        raise LookupError(i)
    return i * i


async def main(n):
    log = []
    t = asyncio.ensure_future(sleeper(log))
    await asyncio.sleep(0)
    t.cancel()
    try:
        await t
    except asyncio.CancelledError:
        log.append('task cancelled')
    total = 0
    for _ in range(n // 10 + 1):
        total += sum(await asyncio.gather(*[gathered(i) for i in range(3)]))
    try:
        await asyncio.gather(*[gathered(i) for i in range(5)])
    except LookupError as e:
        log.append(('gather raised', e.args))
    return log, total


# --- a guard broken while suspended; walkers; a collected generator ---

SCALE = 1


async def scaled(depth):
    if depth == 0:
        return await Step()
    r = await scaled(depth - 1)
    return r * SCALE + depth


def broken_guard(n):
    global SCALE
    out = []
    for i in range(n):
        c = scaled(3)
        c.send(None)
        if i == n - 1:
            SCALE = 2
        out.append(run_coro(c, [i])[-1])
    SCALE = 1
    return out[-3:]


FACTOR = 1


async def rebinds(i, last):
    global FACTOR
    if i == last:
        FACTOR = 3          # while the awaiting frame's unit runs this, nested
    return i


async def reads_after(i, last):
    r = await rebinds(i, last)
    return r * FACTOR


def writes_caller():
    sys._getframe(1).f_locals['x'] = 99
    yield 'wrote'


def written(n):
    x = n
    r = yield from writes_caller()
    return x, r


def nested_runs(n):
    global FACTOR
    out = []
    for i in range(n):
        out.append(run_coro(reads_after(i, n - 1), [])[-1])
    FACTOR = 1
    for i in range(n):
        g = written(i)
        out.append(next(g))
        out.append(next(g, 'end'))
    return out[-4:]


def walk_names():
    f = sys._getframe(1)
    names = []
    while f is not None and len(names) < 6:
        names.append(f.f_code.co_name)
        f = f.f_back
    return names


def walked(depth):
    if depth == 0:
        yield walk_names()
        return
    yield from walked(depth - 1)


async def tb_leaf():
    await Step()
    raise RuntimeError('deep')


async def tb_mid(depth):
    if depth == 0:
        return await tb_leaf()
    return await tb_mid(depth - 1)


def tracebacks():
    c = tb_mid(3)
    c.send(None)
    try:
        c.send(None)
    except RuntimeError as e:
        tb, names = e.__traceback__, []
        while tb is not None:
            names.append(tb.tb_frame.f_code.co_name)
            tb = tb.tb_next
        return names


def guarded_leaf(log):
    try:
        yield 'held'
        yield 'never'
    finally:
        log.append('leaf finally')


def guarded_parent(log):
    try:
        yield from guarded_leaf(log)
    finally:
        log.append('parent finally')


def collected(n):
    log = []
    for _ in range(n):
        g = guarded_parent(log)
        next(g)
        del g
    gc.collect()
    return log[-2:], len(log)


def reentered():
    def g():
        yield next(me)
    me = g()
    try:
        next(me)
    except ValueError as e:
        return str(e)


# --- deep ---

async def deep(depth):
    if depth == 0:
        return await Step()
    return await deep(depth - 1) + 1


def deep_chain(rounds):
    out = []
    for i in range(rounds):
        c = deep(5000)
        c.send(None)
        out.append(run_coro(c, [i])[-1])
    return out


sys.setrecursionlimit(20000)
print(delegations(N))
print(awaiting(N // 10 + 2))
print(exception_state(N // 10 + 2))
print(asyncio.run(main(N)))
print(broken_guard(N))
print(nested_runs(N))
print([next(walked(3)) for _ in range(3)])
print(tracebacks())
print(collected(N))
print(reentered())
print(deep_chain(4))
