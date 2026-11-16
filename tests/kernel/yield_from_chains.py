# Generators going on inside `yield from`, hot: a recursive delegation (a
# tree walked in order), a chain that sends values down and gets returns
# back, delegation to iterators that are not generators, throws, closes
# and inspections of a chain while it is suspended in delegation, an await
# chain, and tracing and monitoring turned on while a chain is suspended
# (each level's RESUME and YIELD_VALUE must report).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300


class Tree:
    def __init__(self, left, value, right):
        self.left = left
        self.value = value
        self.right = right

    def __iter__(self):
        if self.left:
            yield from self.left
        yield self.value
        if self.right:
            yield from self.right


def tree(lo, hi):
    if lo >= hi:
        return None
    mid = (lo + hi) // 2
    return Tree(tree(lo, mid), mid, tree(mid + 1, hi))


def accumulate():
    total = 0
    while True:
        got = yield total
        if got is None:
            return total
        total += got


def relay(depth):
    if depth == 0:
        r = yield from accumulate()
    else:
        r = yield from relay(depth - 1)
    return r + depth


def driven(depth, n):
    g = relay(depth)
    next(g)
    last = 0
    for i in range(n):
        last = g.send(i)
    try:
        g.send(None)
    except StopIteration as e:
        return last, e.value


def mixed(n):
    yield from range(n)
    yield from [n, n + 1]
    yield from (k * 2 for k in range(3))
    r = yield from iter(())
    yield -1 if r is None else r


def catcher(depth):
    if depth == 0:
        while True:
            try:
                yield 'waiting'
            except ValueError as e:
                yield 'caught ' + str(e)
    else:
        yield from catcher(depth - 1)


def guarded(depth, log):
    try:
        if depth == 0:
            yield 1
            yield 2
        else:
            yield from guarded(depth - 1, log)
    finally:
        log.append(depth)


def suspended(n):
    out = []
    c = catcher(4)
    out.append(next(c))
    for i in range(n % 5 + 1):
        out.append(c.throw(ValueError(i)))
        out.append(next(c))
    inner = c
    depth = 0
    while inner.gi_yieldfrom is not None:
        inner = inner.gi_yieldfrom
        depth += 1
    out.append((depth, inner.gi_running, c.gi_suspended))
    log = []
    g = guarded(3, log)
    out.append(next(g))
    g.close()
    out.append((log, g.gi_frame))
    try:
        c.throw(KeyError('k'))
    except KeyError as e:
        out.append(('left', e.args))
    out.append(next(c, 'finished'))
    return out


def run(n):
    t = tree(0, 64)
    total = 0
    for _ in range(n):
        for v in t:
            total += v
    return (total, driven(6, n), list(mixed(n % 7)), sum(mixed(n)), suspended(n), list(tree(0, 5)))


class Step:
    def __await__(self):
        got = yield 'step'
        return got


async def awaiting(depth):
    if depth == 0:
        return await Step()
    return await awaiting(depth - 1) + 1


def awaited(n):
    out = []
    for i in range(n % 3 + 1):
        c = awaiting(5)
        out.append(c.send(None))
        try:
            c.send(i)
        except StopIteration as e:
            out.append(e.value)
    return out


def traced():
    events = []

    def tr(frame, event, arg):
        if frame.f_code.co_name in ('relay', 'accumulate'):
            events.append((frame.f_code.co_name, event, arg if event in ('return', 'call') else None))
        return tr

    g = relay(2)
    next(g)
    g.send(1)
    sys.settrace(tr)
    g.send(2)
    sys.settrace(None)
    g.send(3)
    return events


def monitored():
    mon = sys.monitoring
    seen = []
    mon.use_tool_id(3, 'chains')
    mon.register_callback(3, mon.events.PY_RESUME, lambda code, off: seen.append(('resume', code.co_name)))
    mon.register_callback(3, mon.events.PY_YIELD, lambda code, off, v: seen.append(('yield', code.co_name, v)))
    g = relay(2)
    next(g)
    g.send(1)
    mon.set_events(3, mon.events.PY_RESUME | mon.events.PY_YIELD)
    g.send(2)
    mon.set_events(3, 0)
    g.send(3)
    mon.free_tool_id(3)
    return seen


print(run(N))
print(awaited(N), traced(), monitored())
