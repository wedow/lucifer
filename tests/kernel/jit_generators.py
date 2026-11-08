# Generators, coroutines and async generators run hot enough to compile,
# observed at every point a program can look at them: values across yields
# (ints, floats, objects, the stack of a for loop), send() and throw() into
# a suspended frame, close() running finally, gi_running and gi_frame seen
# from inside and outside, a frame walker during a call made between yields
# and while suspended, f_locals written there, yield from and await chains
# thrown into and closed, generator expressions consumed by builtins, and
# exceptions leaving a generator with their tracebacks. Each output line
# must be the interpreter's.
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 400


def counter(n):
    i = 0
    total = 0.5
    name = 'c'
    while i < n:
        sent = yield i * 2
        if sent is not None:
            total += sent
        i += 1
        name = name + 'x' if len(name) < 5 else 'c'
    return (i, total, name)


def drive_counter():
    out = []
    for k in range(N):
        g = counter(k % 7)
        acc = 0
        try:
            v = next(g)
            while True:
                acc += v
                v = g.send(k % 3 if v % 4 else None)
        except StopIteration as e:
            out.append((acc, e.value))
    return out[-3:], len(out)


def for_loop_stack(xs):
    for x in xs:
        for y in range(x):
            yield x, y
    yield 'done'


def drive_for_loop_stack():
    n = 0
    last = None
    for _ in range(N // 4):
        for item in for_loop_stack([1, 2, 3]):
            n += 1
            last = item
    return n, last


def with_finally(log, n):
    try:
        for i in range(n):
            yield i
    finally:
        log.append('finally %d' % n)


def drive_close():
    log = []
    for k in range(N // 8):
        g = with_finally(log, 5)
        next(g)
        next(g)
        g.close()
    g = with_finally(log, 3)
    s = sum(g)
    log.append(s)
    g = with_finally(log, 4)
    next(g)
    del g
    return log[:2], log[-4:], len(log)


def catcher():
    caught = []
    while True:
        try:
            v = yield len(caught)
        except ValueError as e:
            caught.append(str(e))
            if len(caught) > 3:
                return caught
        except GeneratorExit:
            caught.append('exit')
            raise
        else:
            caught.append(v)


def drive_throw():
    res = []
    for k in range(N // 8):
        g = catcher()
        next(g)
        g.send(k)
        g.throw(ValueError('a%d' % k))
        g.throw(ValueError('b'))
        g.send('s')
        try:
            g.throw(ValueError('c'))
            g.throw(ValueError('d'))
        except StopIteration as e:
            res.append(e.value)
    g = catcher()
    next(g)
    try:
        g.throw(KeyError('unhandled'))
    except KeyError as e:
        res.append(('unhandled', e.args, g.gi_frame))
    return res[-1], res[0], len(res)


def running_probe(holder):
    for i in range(3):
        g = holder[0]
        yield (g.gi_running, g.gi_suspended, g.gi_frame.f_lineno - running_probe.__code__.co_firstlineno)


def drive_running():
    out = []
    for _ in range(N // 8):
        holder = [None]
        g = running_probe(holder)
        holder[0] = g
        out.append(list(g))
        out.append((g.gi_running, g.gi_frame))
    return out[-2:]


def peek_caller():
    f = sys._getframe(1)
    return sorted((k, v) for k, v in f.f_locals.items() if isinstance(v, (int, float, str)))


def walker_gen(n):
    a = 1
    b = 2.5
    s = 'w'
    for i in range(n):
        a += i
        b *= 1.5
        if i == n - 1:
            yield peek_caller()
        else:
            yield a
    yield (a, b, s)


def drive_walkers():
    res = None
    seen = []
    for k in range(N // 4):
        g = walker_gen(5)
        for v in g:
            res = v
        g2 = walker_gen(4)
        next(g2)
        next(g2)
        loc = g2.gi_frame.f_locals
        seen = sorted((k2, v2) for k2, v2 in loc.items() if k2 in ('a', 'b', 's', 'i'))
    return res, seen


def writer_gen():
    x = 1
    while True:
        yield x
        x += 1


def drive_flocals_write():
    out = []
    for k in range(N // 8):
        g = writer_gen()
        next(g)
        next(g)
        g.gi_frame.f_locals['x'] = 100
        out.append(next(g))
    return out[-1], len(out)


def inner(n):
    for i in range(n):
        try:
            got = yield i
        except KeyError:
            got = yield 'inner caught'
        if got == 'stop':
            return 'inner stopped'
    return 'inner done'


def middle(n):
    r = yield from inner(n)
    yield 'middle got ' + r
    return 'middle done'


def outer(n):
    r = yield from middle(n)
    return 'outer: ' + r


def drive_yield_from():
    res = []
    for k in range(N // 8):
        res.append(list(outer(3)))
        g = outer(5)
        next(g)
        res.append(g.throw(KeyError()))
        res.append(next(g))
        res.append(g.send('stop'))
        try:
            next(g)
        except StopIteration as e:
            res.append(e.value)
        g = outer(5)
        next(g)
        g.close()
        res.append((g.gi_frame, g.gi_yieldfrom))
    return res[-7:]


def bad_gen(n):
    for i in range(n):
        if i == n - 1:
            raise ZeroDivisionError('in gen %d' % i)
        yield i


def drive_errors():
    out = []
    for k in range(N // 8):
        try:
            for v in bad_gen(3):
                pass
        except ZeroDivisionError as e:
            tb = traceback.extract_tb(e.__traceback__)
            out.append([(t.name, t.line) for t in tb])
    g = bad_gen(1)
    try:
        next(g)
    except ZeroDivisionError:
        pass
    out.append(list(g))

    def stopper():
        yield 1
        raise StopIteration('leak')
    try:
        list(stopper())
    except RuntimeError as e:
        out.append((type(e).__name__, str(e), type(e.__cause__).__name__))
    return out[-3:]


def drive_genexprs():
    data = list(range(20))
    words = ['a', 'bb', 'ccc']
    res = None
    for k in range(N):
        s = sum(x * x for x in data)
        a = any(x > 18 for x in data)
        b = all(x >= 0 for x in data)
        c = list(x + k for x in data if x % 3 == 0)
        t = tuple(str(x) for x in data[:4])
        j = '-'.join(w.upper() for w in words)
        m = max((x * 7) % 11 for x in data)
        f = 0.0
        for v in (x / 2 for x in data):
            f += v
        res = (s, a, b, c[-1], t, j, m, f)
    escaped = (x for x in data)
    first = next(escaped)
    rest = sum(escaped)
    return res, first, rest


async def leaf(x):
    return x * 2


async def mid(x):
    a = await leaf(x)
    b = await leaf(a)
    return a + b


async def top(n):
    total = 0
    for i in range(n):
        total += await mid(i)
    return total


class Suspend:
    def __await__(self):
        v = yield 'suspended'
        return v


async def waits(n):
    got = []
    for i in range(n):
        got.append(await Suspend())
    return got


def run_coro(c, sends=()):
    sends = list(sends)
    out = []
    try:
        v = c.send(None)
        while True:
            out.append(v)
            v = c.send(sends.pop(0) if sends else None)
    except StopIteration as e:
        return out, e.value


def drive_coroutines():
    res = []
    for k in range(N // 4):
        res.append(run_coro(top(5)))
        res.append(run_coro(waits(3), [k, 'b', 'c']))
    c = waits(2)
    c.send(None)
    try:
        c.throw(ValueError('into coro'))
    except ValueError as e:
        res.append(('thrown', str(e), c.cr_frame))
    c = waits(2)
    c.send(None)
    c.close()
    res.append((c.cr_running, c.cr_frame, c.cr_await))
    return res[-4:]


async def agen(n):
    for i in range(n):
        try:
            x = yield i
        except ValueError:
            yield 'agen caught'
        if x is not None:
            yield 'got %r' % (x,)


def step(aw):
    try:
        aw.send(None)
    except StopIteration as e:
        return e.value
    except StopAsyncIteration:
        return 'stop'
    except Exception as e:
        return 'raised ' + type(e).__name__
    return 'pending'


def drive_async_gens():
    out = []
    for k in range(N // 8):
        g = agen(3)
        vals = [step(g.__anext__()) for _ in range(4)]
        out.append(vals)
        g = agen(3)
        step(g.asend(None))
        out.append(step(g.asend('v')))
        out.append(step(g.athrow(ValueError())))
        out.append(step(g.aclose()))
        out.append(g.ag_frame)
    return out[-5:]


def pairs(xs, scale):
    total = 0
    k = 0.5
    for x in xs:
        total += x
        k *= scale
        yield x, total, k


def drive_handoff():
    """A generator a compiled loop resumes in line, then the interpreter,
    then the loop again: its frame passes between them at each yield."""
    out = []
    data = list(range(12))
    for r in range(N // 4):
        g = pairs(data, 1.5)
        first = []
        for item in g:
            first.append(item[1])
            if item[0] == 3:
                break
        mid = next(g)
        rest = [t for _, t, _ in g]
        h = pairs(data, 2.0)
        inner = []
        for a in h:
            for b in h:
                inner.append((a[0], b[0]))
                if len(inner) % 3 == 0:
                    break
        out.append((first, mid, rest[-1], inner[-2:], any(x > 10 for x in data), all(x < 5 for x in data)))
    return out[-1], len(out)


def drive_resumed_errors():
    """Exceptions raised in a generator a compiled loop resumes in line."""
    out = []
    def odd(n):
        for i in range(n):
            if i == 5:
                out.append(sum(1 for _ in range(i)) // 0)
            yield i
    for r in range(N // 8):
        try:
            for v in odd(8):
                pass
        except ZeroDivisionError as e:
            out.append([(t.name, t.line) for t in traceback.extract_tb(e.__traceback__)])
    return out[-1], len(out)


for name, fn in [('counter', drive_counter), ('for_loop_stack', drive_for_loop_stack), ('close', drive_close),
                 ('throw', drive_throw), ('running', drive_running), ('walkers', drive_walkers),
                 ('flocals_write', drive_flocals_write), ('yield_from', drive_yield_from),
                 ('errors', drive_errors), ('genexprs', drive_genexprs), ('coroutines', drive_coroutines),
                 ('async_gens', drive_async_gens), ('handoff', drive_handoff),
                 ('resumed_errors', drive_resumed_errors)]:
    print(name, fn())
