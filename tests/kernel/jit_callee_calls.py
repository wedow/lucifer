# Hot calls the optimizing tier inlines once the interpreter has recorded
# their callees: methods of four classes at one site, a function in a local
# and in an attribute, a bound method, a staticmethod through self, a
# closure in an attribute and two functions at one site. tools/jitcheck
# runs this at two sizes and requires that compiled code's calls it did not
# inline, of callees it could not name or of a polymorphic site's methods,
# do not grow with the iterations. Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    def __init__(self, v):
        self.v = v

    def get(self, k):
        return self.v + k


class B(A):
    def get(self, k):
        return self.v - k


class C(A):
    def get(self, k):
        return self.v * k


class D(A):
    def get(self, k):
        return k


def poly(objs, n):
    s = 0
    for i in range(n):
        s += objs[i & 3].get(i)
    return s


def inc(x):
    return x + 1


def dec(x):
    return x - 1


def maker(k):
    def add_k(x):
        return x + k
    return add_k


class Holder:
    def __init__(self, fn):
        self.fn = fn

    def add(self, x):
        return x + 2

    @staticmethod
    def static(x):
        return x - 2

    def run(self, n):
        s = 0
        for i in range(n):
            s += self.fn(i) + self.static(i)
        return s


def local(n, f, m):
    s = 0
    for i in range(n):
        s += f(i) + m(i)
    return s


def two(n, fs):
    s = 0
    for i in range(n):
        s += fs[i & 1](i)
    return s


print(poly([A(1), B(2), C(3), D(4)], N))
print(local(N, inc, Holder(None).add))
print(Holder(inc).run(N), Holder(maker(3)).run(N))
print(two(N, [inc, dec]))
