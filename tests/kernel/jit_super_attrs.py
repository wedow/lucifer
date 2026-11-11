# super().name in methods compiled hot, inlined into their callers or not,
# with instances of the method's class and of its subclasses: a
# function and a method descriptor in the method form, a plain class value,
# a classmethod, a property and a missing name (the generic path), a diamond
# MRO, and the base classes changing under the compiled code (a method
# replaced, one added between, __bases__ reassigned).

N = 3000


class Base:
    tag = 'base'

    def f(self, x):
        return x + 1

    @classmethod
    def cm(cls):
        return 'cm ' + cls.__name__

    @property
    def p(self):
        return 'p'


class Mid(Base):
    def f(self, x):
        return super().f(x) * 2

    def tag_of(self):
        return super().tag

    def cm2(self):
        return super().cm()

    def p2(self):
        return super().p

    def missing(self):
        try:
            return super().nothing
        except AttributeError as e:
            return 'AttributeError'


class Leaf(Mid):
    pass


class Scope(dict):
    def get(self, key, default=None):
        v = super().get(key, default)
        return v if v is not None else 'none'


class L(Base):
    def f(self, x):
        return super().f(x) + 100


class R(Base):
    def f(self, x):
        return super().f(x) + 10


class D(L, R):
    pass


def hot(o, n):
    out = set()
    for i in range(n):
        out.add(o.f(i % 3))
    return sorted(out)


def hot_names(o, n):
    out = set()
    for i in range(n):
        out.add((o.tag_of(), o.cm2(), o.p2(), o.missing()))
    return sorted(out)


def hot_get(s, n):
    out = set()
    for i in range(n):
        out.add(str(s.get('a')))
        out.add(str(s.get('b')))
    return sorted(out)


def hot_mixed(objs, n):
    out = set()
    for i in range(n):
        out.add(objs[i % len(objs)].f(i % 3))
    return sorted(out)


print(1, hot(Leaf(), N), hot(Mid(), 4), hot_mixed([Mid(), Leaf(), D()], N))
print(2, hot_names(Leaf(), N))
print(3, hot_get(Scope(a=1), N))
print(4, hot(D(), N))

Base.f = lambda self, x: x - 1             # replaced on the base after compiling
print(5, hot(Leaf(), 4), hot(D(), 4))
Base.tag = 'retagged'
print(5, hot_names(Leaf(), 1))


class Between(Base):
    def f(self, x):
        return 1000


Mid.__bases__ = (Between,)
print(6, hot(Leaf(), 4))
