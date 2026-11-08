# Class attributes that are plain values of a mutable type, read through a
# class compiled code knows (a global): Enum members in a hot method that
# inlines, and an instance of a user class. Compiled code takes the value as
# a constant behind the versions of the class, its metaclass and the value's
# type, so each change below must show at once: the value's type gains a
# __get__, the class rebinds the name, the metaclass gains it as a property.
# Each result must be the interpreter's.
from enum import Enum
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Direction(Enum):
    FORWARD = 1
    BACKWARD = 2


class Edge:
    def __init__(self, d):
        self.d = d

    def forward(self):
        return self.d is Direction.FORWARD


def count(edges, n):
    s = 0
    for i in range(n):
        s += edges[i % len(edges)].forward()
    return s


class Value:
    def __init__(self, v):
        self.v = v


class Meta(type):
    pass


class Config(metaclass=Meta):
    unit = Value(3)


def total(n):
    s = 0
    for i in range(n):
        u = Config.unit
        s += u if isinstance(u, int) else u.v
    return s


print(count([Edge(Direction.FORWARD), Edge(Direction.BACKWARD)], N))
print(total(N))
Value.__get__ = lambda self, obj, cls: 7
print(total(N))
del Value.__get__
print(total(N))
Config.unit = Value(5)
print(total(N))
Meta.unit = property(lambda cls: 11)
print(total(N))
del Meta.unit
print(total(N))
