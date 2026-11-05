# Attribute sites that meet several classes, which compiled code answers
# from a small cache keyed by each class's version: instance values, methods
# and plain class attributes of up to four classes, then a fifth class, a
# class changed while the code runs (its version changes), an instance that
# moves its attributes to a dict, and classes the collector frees while a
# cache still names their methods.
import gc

N = 3000


class A:
    kind = 'a'

    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


class B(A):
    kind = 'b'

    def get(self):
        return self.v * 2


class C:
    kind = 'c'

    def __init__(self, v):
        self.w = 0
        self.v = v

    def get(self):
        return -self.v


class D(C):
    kind = 'd'


class E(A):
    def get(self):
        return 100


def walk(objs, n):
    total = 0
    kinds = ''
    for i in range(n):
        o = objs[i % len(objs)]
        total += o.v + o.get()
        if i < len(objs):
            kinds += o.kind
    return total, kinds


objs = [A(1), B(2), C(3), D(4)]
print(walk(objs, N))
print(walk(objs + [E(5)], N))
B.get = lambda self: 7
print(walk(objs, N))
objs[0].__dict__['extra'] = 1           # the attributes move to a dict
print(walk(objs, N))
del B.get
print(walk(objs, N))


def make(i):
    class F:
        kind = 'f'

        def __init__(self):
            self.v = i

        def get(self):
            return i
    return F()


for r in range(5):
    tmp = [make(i) for i in range(4)]
    print(walk(tmp, N // 3))
    del tmp
    gc.collect()
print(walk(objs, N))

# Threads fill one site's cache at once.
import threading


def walker(cls_list, out):
    objs = [k(i) for i, k in enumerate(cls_list)]
    out.append(walk(objs, N))


class G(A):
    pass


class H(C):
    pass


results = []
ts = [threading.Thread(target=walker, args=([A, B, C, D, G, H][k:k + 3], results)) for k in range(4)]
for t in ts:
    t.start()
for t in ts:
    t.join()
print(sorted(results))
