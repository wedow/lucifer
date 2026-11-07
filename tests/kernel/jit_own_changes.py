# Compiled code relies on globals, class attributes, a class's bases and
# a function's code between polls; a change the running thread makes, in
# the loop itself, in a call or in a finalizer, is seen by the very next
# read, in the same iteration.


class Base1:
    x = 1


class Base2:
    x = 2


class Leaf(Base1):
    pass


class Helper:
    def compute(self):
        return 10


def g():
    return 100


def g2():
    return 200


def f():
    return 'f'


def patch_all():
    global g
    g = g2
    Helper.compute = lambda self: 20
    Leaf.__bases__ = (Base2,)
    f.__code__ = (lambda: 'f2').__code__


def reset():
    global g
    g = g_first
    Helper.compute = compute_first
    Leaf.__bases__ = (Base1,)
    f.__code__ = code_first


def read(helper, leaf):
    return g(), helper.compute(), Helper.compute(helper), Leaf.x, leaf.x, f()


g_first = g
compute_first = Helper.compute
code_first = f.__code__


# A call in the loop makes every change.
def by_call(n, helper, leaf):
    seen = []
    for i in range(n):
        before = read(helper, leaf)
        if i == n - 2:
            patch_all()
        seen.append((before, read(helper, leaf)))
    return seen[-3:]


# The loop makes them itself.
def in_loop(n, helper, leaf):
    global g
    seen = []
    for i in range(n):
        before = (g(), Helper.compute(helper), Leaf.x)
        if i == n - 2:
            g = g2
            Helper.compute = lambda self: 20
            Leaf.__bases__ = (Base2,)
        seen.append((before, (g(), Helper.compute(helper), Leaf.x)))
    return seen[-3:]


# A finalizer the loop's own store runs makes them.
class Patcher:
    def __del__(self):
        patch_all()


def by_finalizer(n, helper, leaf):
    seen = []
    p = None
    for i in range(n):
        if i == n - 2:
            p = Patcher()
        before = read(helper, leaf)
        p = None
        seen.append((before, read(helper, leaf)))
    return seen[-3:]


for test in (by_call, in_loop, by_finalizer):
    for n in (5, 300, 3000):
        print(test.__name__, n, test(n, Helper(), Leaf()))
        reset()
