# A raise three levels under each way a call enters a function (a plain call,
# a callee known only as an argument, a method, a call from C, a generator's
# turn, a callee run in line): the traceback's functions, lines and
# instruction offsets, the f_back chain and its identity with sys._getframe,
# each caller's f_lineno, a frame object kept past the return with its
# f_locals, and an argument-binding TypeError at a hot call site.
import sys


def rel(code, line):
    return line - code.co_firstlineno


def fail(x):
    stack.append([(f.f_code.co_name, rel(f.f_code, f.f_lineno)) for f in walk(sys._getframe())])
    same.append(all(sys._getframe(k) is f for k, f in enumerate(walk(sys._getframe()))))
    raise ValueError(x)


def walk(f):
    out = []
    while f is not None and f.f_code.co_name != '<module>':
        out.append(f)
        f = f.f_back
    return out


def mid(x):
    y = x * 2
    return fail(y)


def plain(x):
    return mid(x)


def by_argument(fn, x):
    return fn(x)


class C:
    def m(self, x):
        return mid(x)


def via_c(x):
    return sorted([x], key=mid)


def gen(x):
    yield mid(x)


def via_gen(x):
    return next(gen(x))


WAYS = {
    'plain': plain,
    'argument': lambda x: by_argument(mid, x),
    'method': lambda x: C().m(x),
    'C': via_c,
    'generator': via_gen,
}


def tb_entries(e):
    tb, out = e.__traceback__, []
    while tb is not None:
        code = tb.tb_frame.f_code
        out.append((code.co_name, rel(code, tb.tb_lineno), tb.tb_lasti >= 0))
        tb = tb.tb_next
    return out


stack, same = [], []


def run(way, x):
    try:
        WAYS[way](x)
    except ValueError as e:
        return tb_entries(e)


for i in range(300):
    for way in WAYS:
        run(way, i)
for way in WAYS:
    stack.clear()
    same.clear()
    print(way, run(way, 7), stack, same)


# A frame object kept past the return, with its locals.
kept = []


def keeper(n):
    a = n + 1
    b = [n]
    kept.append(sys._getframe())
    return a


def calls_keeper(n):
    s = 0
    for i in range(n):
        s += keeper(i)
    return s


calls_keeper(300)
kept.clear()
calls_keeper(2)
print('kept', [(f.f_code.co_name, sorted(f.f_locals.items()), f.f_lineno - keeper.__code__.co_firstlineno,
                f.f_back.f_code.co_name if f.f_back else None) for f in kept])
kept.clear()


# A binding TypeError at a hot call site.
def two(a, b):
    return a + b


def binder(fn, n):
    s = 0
    for i in range(n):
        s += fn(i, 1)
    return s


binder(two, 300)
two.__defaults__ = None
for args in ((1,), (1, 2, 3)):
    try:
        two(*args)
    except TypeError as e:
        print('TypeError', e)
try:
    binder(lambda a: a, 3)
except TypeError as e:
    print('TypeError at the site', e, [n for n, *_ in tb_entries(e)])
