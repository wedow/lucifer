# References across calls compiled code makes to callees it does not take
# in line: what a callee returns, the arguments it is given and the
# callable itself are released as the interpreter releases them, so an
# object dies when its last reference goes, as weak references show.
import sys
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class C:
    pass


def make0():
    return C()


def make1():
    return C()


def ident(x):
    return x


def run(n):
    makers = [make0, make1, C, ident]
    dead = 0
    for i in range(n):
        f = makers[i % 4]
        obj = f(C()) if f is ident else f()
        r = weakref.ref(obj)
        del obj
        dead += r() is None
    return dead


def kept(n):
    # The same callable kept in a local and called through it: the call's
    # operand and the local both name it.
    out = []
    for i in range(n):
        f = make0 if i & 1 else make1
        g = f
        out.append(weakref.ref(g()))
    return sum(r() is None for r in out)


print(run(N))
print(kept(N))
a, b = [C() for i in range(2)]
wa = weakref.ref(a)
del a
print(wa() is None, b is not None)
