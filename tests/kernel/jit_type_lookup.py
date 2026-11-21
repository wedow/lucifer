import sys

try:
    from _lucifer import MISSING, type_lookup
except ImportError:
    MISSING = object()

    def type_lookup(cls, name):
        for base in type.__getattribute__(cls, '__mro__'):
            attrs = vars(base)
            if name in attrs:
                return attrs[name]
        return MISSING


class Base:
    marker = object()


class Derived(Base):
    pass


class Other:
    marker = object()


def scan(cls, expected, n):
    # Both lookups receive a class from the caller. The growth check
    # requires the inherited hit and the miss to fold in the hot loop.
    hits = 0
    for _ in range(n):
        if type_lookup(cls, 'marker') is expected:
            hits += 1
        if type_lookup(cls, 'absent') is MISSING:
            hits += 1
    return hits


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
print(scan(Derived, Base.marker, n))
print(scan(Other, Other.marker, 3))
Base.marker = object()
print(scan(Derived, Base.marker, 3))
Derived.absent = object()
print(scan(Derived, Base.marker, 3))
del Base.marker
print(scan(Derived, MISSING, 3))
