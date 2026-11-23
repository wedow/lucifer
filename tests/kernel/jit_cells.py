# Cells read and written by hot code: free variables of closures the
# caller learned (each call's own cells) and of a closure that is a
# constant (a module global, whose cell's value compiled code may hold),
# a cell two closures share, nonlocal writes from the inner and the outer
# function while a loop reads, a cell variable deleted and read unbound
# (NameError, UnboundLocalError), a generator closing over a loop
# variable, and class bodies with a __class__ cell. Each output line
# must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def mk(k):
    def add(v):
        return v + k
    return add


def pair(k):
    def get():
        return k

    def put(v):
        nonlocal k
        k = v
    return get, put


adder = mk(3)
get, put = pair(10)


def reads(n):
    f = mk(5)
    s = 0
    for j in range(n):
        s += f(j) + adder(j) + get()
        if j == n // 2:
            put(20)                 # the shared cell changes under the loop
    return s


def outer_writes(n):
    k = 1

    def peek():
        return k
    s = 0
    for j in range(n):
        s += peek()
        if j % 1000 == 999:
            k += 1                  # the outer function's own write
    return s


def inner_writes(n):
    count = 0

    def bump(by):
        nonlocal count
        count += by
        return count
    s = 0
    for j in range(n):
        s += bump(j & 3)
    return s + count


def deleted(n):
    x = 1

    def see():
        return x
    out = []
    for j in range(n):
        try:
            out.append(see())
        except NameError as e:
            out.append(str(e))
        if j == n // 2:
            del x
    return out[0], out[-1], len(out)


def unbound_local(n):
    out = []
    for j in range(n):
        def f():
            if j < 0:
                y = 1
            g = lambda: y
            return g()
        try:
            out.append(f())
        except NameError as e:
            out.append(type(e).__name__ + ': ' + str(e))
        try:
            def h():
                print(z)
                z = 1
                return lambda: z
            h()
        except UnboundLocalError as e:
            out.append(str(e))
    return out[:2], len(out)


def gen_closures(n):
    fs = []
    for j in range(5):
        fs.append((lambda: j * 10) if j % 2 else (lambda j=j: j))
    gens = [(x + j for x in range(3)) for j in range(4)]
    s = 0
    for _ in range(n // 10):
        s += sum(f() for f in fs)
    return s, [list(g) for g in gens]


class Base:
    def who(self):
        return 'base'


class Child(Base):
    def who(self):
        return 'child>' + super().who() + ':' + __class__.__name__


def classes(n):
    c = Child()
    s = 0
    for _ in range(n):
        s += len(c.who())
    return s, c.who()


print(reads(N))
print(get())
print(outer_writes(N))
print(inner_writes(N))
print(deleted(N))
print(unbound_local(N // 10))
print(gen_closures(N))
print(classes(N))
