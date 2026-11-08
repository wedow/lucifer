# Classmethods and staticmethods called through their class, as DeltaBlue
# calls Strength.stronger, from methods the hot loop inlines.
# tools/jitcheck runs this at two sizes and requires that no call per
# iteration enters compiled code (entries, direct and compiled calls): the
# load binds the callable at compile time and the call inlines. Then the
# class's attribute and the staticmethod's own callable change under the
# compiled code, and both kinds are called through instances (of a type
# the code knows, and not), and shadowed by an instance's attribute.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Strength:
    def __init__(self, v):
        self.v = v

    @classmethod
    def stronger(cls, a, b):
        return a.v < b.v

    @classmethod
    def weakest(cls, a, b):
        return b if cls.stronger(a, b) else a

    @staticmethod
    def pick(a, b):
        return a if a.v > b.v else b


class Constraint:
    def __init__(self, v):
        self.s = Strength(v)

    @staticmethod
    def twice(x):
        return x * 2

    @classmethod
    def unit(cls):
        return cls is Constraint

    def step(self, other):
        if Strength.stronger(self.s, other.s):
            return Strength.weakest(self.s, other.s).v
        return Strength.pick(self.s, other.s).v

    def through_self(self, x):
        return self.twice(x) + self.unit()


def through_instances(n):
    s = 0
    for i in range(n):
        c = Constraint(i)       # a type the code knows
        s += c.twice(i) + c.unit() + c.through_self(i)
    return s


def hot(n):
    a, b = Constraint(1), Constraint(2)
    s = 0
    for i in range(n):
        s += a.step(b) + b.step(a)
    return s


print(hot(N))
print(through_instances(300))
Strength.__dict__['pick'].__init__(lambda a, b: b)
print(hot(3))
Strength.pick = staticmethod(lambda a, b: a)
print(hot(3))
Strength.stronger = classmethod(lambda cls, a, b: cls is Strength and a.v > b.v)
print(hot(3))
c = Constraint(5)
c.twice = lambda x: -x
print(c.through_self(9), Constraint(9).through_self(9))
Constraint.twice = staticmethod(lambda x: x + 1000)
print(hot(3), through_instances(3))
