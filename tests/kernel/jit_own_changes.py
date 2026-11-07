# Compiled code relies on globals, class attributes, a class's bases and
# a function's code between polls. A change the running thread makes in
# compiled code, through a call it cannot inline, its own stores or a
# finalizer its own store runs, is seen by the very next read, in the
# same iteration.


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


def compute2(self):
    return 20


def patch_all():
    global g
    g = g2
    Helper.compute = compute2
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
G = globals()


def maybe_patch(i, n):
    if i == n - 2:
        patch_all()


# A call the loop cannot inline (its callee comes from a list) makes them.
def by_call(n, helper, leaf):
    act = hooks[0]
    seen = []
    for i in range(n):
        before = read(helper, leaf)
        act(i, n)
        seen.append((before, read(helper, leaf)))
    return seen[-3:]


hooks = [maybe_patch]


# The loop's own stores make them, every iteration; the values change two
# iterations before the end.
def by_store(n, helper, leaf):
    gs = [g_first] * (n - 2) + [g2] * 2
    cs = [compute_first] * (n - 2) + [compute2] * 2
    bs = [(Base1,)] * (n - 2) + [(Base2,)] * 2
    seen = []
    for i in range(n):
        before = (g(), Helper.compute(helper), Leaf.x, leaf.x)
        G['g'] = gs[i]
        Helper.compute = cs[i]
        Leaf.__bases__ = bs[i]
        seen.append((before, (g(), Helper.compute(helper), Leaf.x, leaf.x)))
    return seen[-3:]


# A finalizer the loop's own store runs makes them.
class Patcher:
    def __del__(self):
        patch_all()


def by_finalizer(n, helper, leaf):
    ps = [None] * n
    ps[n - 2] = Patcher()
    seen = []
    for i in range(n):
        p = ps[i]
        ps[i] = None
        before = read(helper, leaf)
        p = None
        seen.append((before, read(helper, leaf)))
    return seen[-3:]


for test in (by_call, by_store, by_finalizer):
    for n in (5, 300, 3000):
        print(test.__name__, n, test(n, Helper(), Leaf()))
        reset()
