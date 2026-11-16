# Generators made and consumed in one piece of compiled code: the for loop
# runs the generator's body from where it stood to its next yield, and the
# generator, its frame and the values it yields need not exist. An exit
# must find the generator as it stands (suspended between items, or
# running, with its frame above the consumer's), a generator the program
# takes out of the loop must go on where the loop left it, and one dropped
# inside a try block must run its finally. tools/jitcheck also runs this
# at two sizes and requires that the resumes the interpreter runs and the
# ints and tuples compiled code allocates do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def count(n):
    for i in range(n):
        yield i


def pairs(seq):
    k = 0
    for x in seq:
        yield k, x
        k += 2


def evens(n):
    i = 0
    while i < n:
        if i & 1 == 0:
            yield i
        i += 1


def two_yields(n):
    i = 0
    while i < n:
        yield i
        yield -i
        i += 3


def fused_count(n):
    s = 0
    for i in count(n):
        s += i
    return s


def fused_pairs(seq):
    s = 0
    for k, x in pairs(seq):
        s += k * x
    return s


def fused_evens(n):
    s = 0
    for i in evens(n):
        s += i
    return s


def fused_two(n):
    s = 0
    for i in two_yields(n):
        s += i & 7
    return s


L = [3, 1, 4, 1, 5, 9, 2, 6]


def step(i):
    return fused_count(9) + fused_pairs(L) + fused_evens(7) + fused_two(8) + (i & 3)


step(0)


def hot(n):
    s = 0
    for i in range(n):
        s += step(i)
    return s


print('hot', hot(N))

# The rest runs a fixed number of times, whatever N is.
K = 300


def exits_between_items():
    """A guard fails in the consumer's body: the generator is suspended."""
    out = []
    for r in range(K):
        s = 0
        for i in count(8):
            s = s * 1000003 * 1000003 * 1000003 if r % 50 == 49 and i == 5 else s + i
        out.append(s % 1000033)
    return out[-5:]


print('exits_between_items', exits_between_items())


def growing(n, r):
    big = 1
    for i in range(n):
        big = big * 1000003 * 1000003 if r % 40 == 39 and i == 3 else big + i
        yield big


def exits_in_body():
    """A guard fails in the generator's body: it is running, above the loop."""
    out = []
    for r in range(K):
        s = 0
        for v in growing(6, r):
            s += v % 1000
        out.append(s)
    return out[277:281]


print('exits_in_body', exits_in_body())


def taken_out():
    out = []
    for r in range(K):
        g = count(6)
        for i in g:
            if i == r % 4:
                break
        out.append((next(g, None), list(g), next(g, 'done')))
    return out[-4:]


print('taken_out', taken_out())

LOG = []


def guarded(n):
    try:
        for i in range(n):
            yield i
    finally:
        LOG.append(n)


def dropped_in_try():
    """A generator left suspended inside try runs its finally when dropped."""
    for r in range(K):
        for i in guarded(5 + r % 2):
            if i == 2 and r % 3:
                break
    return len(LOG), LOG[-6:]


print('dropped_in_try', dropped_in_try())


def frames(n):
    for i in range(n):
        yield (sys._getframe().f_code.co_name, sys._getframe(1).f_code.co_name, i)


def seen_by_frame():
    out = []
    for r in range(K):
        for t in frames(3):
            if r % 100 == 99:
                out.append(t)
    return out[-4:]


print('seen_by_frame', seen_by_frame())


def failing(n, r):
    for i in range(n):
        if r % 60 == 59 and i == 2:
            raise ValueError(i)
        yield i


def raises_in_body():
    out = []
    for r in range(K):
        g = failing(4, r)
        try:
            for i in g:
                out.append(i)
        except ValueError as e:
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                tb = tb.tb_next
            out.append((names, next(g, 'finished'), g.gi_frame))
    return out[-8:]


print('raises_in_body', raises_in_body())


def returns_early(n):
    for i in range(n):
        if i == 3:
            return 'early'
        yield i


def finishes():
    out = []
    for r in range(K):
        g = returns_early(2 + r % 4)
        s = 0
        for i in g:
            s += i
        out.append((s, next(g, 'done'), g.gi_frame is None))
    return out[-4:]


print('finishes', finishes())


def state_kept(seq):
    total = 0
    for x in seq:
        total += x
        got = yield total
        if got is not None:
            total = got


def sent_after():
    """The loop's generator is then driven by send()."""
    out = []
    for r in range(K):
        g = state_kept([1, 2, 3, 4, 5])
        for v in g:
            if v >= 3:
                break
        out.append((g.send(100 + r), next(g)))
    return out[-3:]


print('sent_after', sent_after())


def nested(rows):
    for row in rows:
        for x in count(row):
            yield x * 2


def nested_total():
    out = []
    for r in range(K):
        s = 0
        for v in nested((3, r % 4, 2)):
            s += v
        out.append(s)
    return out[-4:]


print('nested_total', nested_total())


def genexp_total():
    out = []
    data = (1, 2, 3, 4)
    for r in range(K):
        s = 0
        for v in (x * r for x in data if x != 3):
            s += v
        out.append(s)
    return out[-3:]


print('genexp_total', genexp_total())
