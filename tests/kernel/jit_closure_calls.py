# Calls of functions made anew each time (closures, lambdas, nested
# defs): the site knows them by their code, and each call runs the
# function called: its own cells, its own defaults, its own identity in
# a frame that something looks at or that an exception unwinds.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def adder(k):
    def add(x):
        return x + k
    return add


def counter():
    n = 0

    def bump(by):
        nonlocal n
        n += by
        return n
    return bump


def with_default(k):
    def f(x, y=k * 2):
        return x + y + k
    return f


def apply(f, x):
    return f(x)


def cells(n):
    total = 0
    for i in range(n):
        total += apply(adder(i), 1)
        total += apply(lambda x: x * i, 2)
    return total


def shared_cell(n):
    out = 0
    for i in range(n // 10):
        b = counter()
        for j in range(10):
            out += b(j)
        out += b.__closure__[0].cell_contents
    return out


def defaults(n):
    total = 0
    for i in range(n):
        f = with_default(i)
        total += f(1)           # the default is this function's own
        total += f(1, 1)
    return total


def identity(n):
    seen = []

    def make(i):
        def who():
            return sys._getframe(0).f_code.co_name, i
        return who

    for i in range(n):
        w = make(i)
        r = w()
        if i >= n - 3:
            seen.append(r)
    return seen


def raising(n):
    caught = []

    def make(i):
        def boom(x):
            if x == n - 2:
                raise ValueError(i)
            return x
        return boom

    for i in range(n):
        f = make(i)
        try:
            f(i)
        except ValueError as e:
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                last = tb.tb_frame
                tb = tb.tb_next
            caught.append((e.args, names, last.f_locals['x'], sorted(last.f_locals)))
    return caught


def mixed(n):
    # The site meets a closure of another code, a bound method, a class.
    fs = [adder(1)] * (n - 4) + [counter(), [].append, int, adder(5)]
    out = []
    for f in fs:
        out.append(f(3))
    return out[-6:], len(out)


def rebound(n):
    # A cell rebound between calls of the same function object.
    k = 1

    def f(x):
        return x * k

    t = 0
    for i in range(n):
        t += f(i)
        if i % 100 == 0:
            k += 1
    return t


def temporary(n):
    # The function is on the stack only and dies with its call's operands.
    t = 0
    for i in range(n):
        t += adder(i)(i)
        t += (lambda a, b=i: a - b)(i)
    return t


for f in (cells, shared_cell, defaults, identity, raising, mixed, rebound, temporary):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 6))
