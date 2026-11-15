# Generators whose yields compiled code makes itself: what the frame
# holds while suspended is what the interpreter's would hold. Locals that
# are numbers, tuples and lists compiled code never made, one object in
# several locals, an unbound local, values on the stack across the yield,
# sent values, a frame looked at while suspended, a generator closed,
# thrown into, abandoned mid-way and finished; finalizers of what a
# suspended frame keeps alive.
import gc
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def numbers(n):
    total = 0
    scale = 1.5
    for i in range(n):
        total += i
        pair = (i, total)
        yield pair[0] * scale + pair[1]
        if i % 7 == 0:
            scale += 0.5


def aliases(n):
    for i in range(n):
        a = [i]
        b = a
        c = (a, b)
        yield a
        b.append(1)
        yield c[0] is b, len(a)


def unbound(n):
    for i in range(n):
        if i % 3 == 0:
            late = i
        yield i
        if i % 3 == 1:
            del late
        try:
            yield late
        except UnboundLocalError:
            yield -1


def on_stack(n):
    for i in range(n):
        x = [i, (yield i), i * 2, (yield -i)]
        yield x


def echo(n):
    got = None
    for i in range(n):
        got = yield (i, got)
    return 'done', got


def drive_stack(n):
    out = []
    g = on_stack(n)
    v = next(g)
    try:
        while True:
            out.append(v)
            v = g.send(len(out))
    except StopIteration:
        pass
    return out[-6:], len(out)


def drive_echo(n):
    g = echo(n)
    out = [next(g)]
    try:
        for i in range(n + 2):
            out.append(g.send(i * 1.5 if i % 2 else str(i)))
    except StopIteration as e:
        out.append(e.value)
    return out[:3], out[-3:]


def looked_at(n):
    g = numbers(n)
    seen = []
    for k, v in enumerate(g):
        if k >= n - 3 or k == 5:
            f = g.gi_frame
            loc = f.f_locals
            seen.append((v, loc['i'], loc['total'], loc['pair'], loc['scale'], f.f_lineno - f.f_code.co_firstlineno,
                         loc['pair'] is g.gi_frame.f_locals['pair'], g.gi_running, g.gi_suspended))
    return seen


class Loud:
    log = []

    def __init__(self, name):
        self.name = name

    def __del__(self):
        Loud.log.append(self.name)


def holding(n):
    for i in range(n):
        keep = Loud(i)
        tmp = Loud(-i - 1)
        del tmp
        yield i
        try:
            yield keep.name
        finally:
            keep = None


def lifetimes(n):
    Loud.log.clear()
    g = holding(n)
    out = []
    for k, v in enumerate(g):
        out.append((v, len(Loud.log)))
        if k == 2 * n - 3:
            break
    before = len(Loud.log)
    g.close()
    closed = len(Loud.log)
    g2 = holding(n)
    next(g2)
    del g2
    gc.collect()
    return out[-4:], before, closed, len(Loud.log), Loud.log[-3:]


def thrown(n):
    def body():
        hits = 0
        for i in range(n):
            try:
                yield i
            except KeyError as e:
                hits += 1
                yield ('caught', i, hits, e.args)
        return hits

    g = body()
    out = []
    for i in range(n):
        v = next(g)
        if i % 500 == 3:
            out.append(g.throw(KeyError(i)))
        if i == n - 1:
            try:
                g.throw(ValueError('stop'))
            except ValueError as e:
                out.append(('escaped', e.args, g.gi_frame))
    return out


def nested(n):
    def inner(k):
        for j in range(k):
            yield j * 2

    def outer():
        for i in range(n // 10):
            for v in inner(i % 5):
                yield i + v
            yield from inner(2)

    return sum(outer()), sum(1 for _ in outer())


def exhausted(n):
    g = numbers(3)
    out = list(g)
    out.append(next(g, 'none'))
    out.append(g.gi_frame)
    return out


print('numbers', sum(numbers(N)), sum(numbers(N // 7)))
print('aliases', [x for x in aliases(N)][-4:])
print('unbound', list(unbound(N))[-9:], sum(unbound(N // 3)))
for f in (drive_stack, drive_echo, looked_at, lifetimes, thrown, nested, exhausted):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 9))
