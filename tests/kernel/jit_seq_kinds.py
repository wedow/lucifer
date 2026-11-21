# luc-tax0: kernel code that reads tuples and lists whose exact class its
# sites keep changing (tuple.__eq__ on namedtuples of four classes, a tuple
# subclass and plain tuples; list.__eq__ on lists and list subclasses)
# reads their sizes and items in line under a guard on the kind every class
# it met shares, and decides kind() from the class's flags. tools/jitcheck
# runs this at two sizes and requires that the helper calls compiled code
# makes do not grow with the length of what it compares: a few per
# comparison, not a few per item. What the comparisons, sizes and items
# answer must be the interpreter's, also in dict lookups that compare keys.
import collections
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

FIELDS = ' '.join(f'f{i}' for i in range(40))
A = collections.namedtuple('A', FIELDS)
B = collections.namedtuple('B', FIELDS)
C = collections.namedtuple('C', FIELDS)
D = collections.namedtuple('D', FIELDS)


class E(tuple):
    pass


class L(list):
    pass


class M(list):
    def __len__(self):
        return 99


def make(cls, items):
    return cls(*items) if cls not in (E, tuple) else cls(items)


def tuples(n):
    base = list(range(40))
    xs = [make(cls, base) for cls in (A, B, C, D, E, tuple)]
    ys = [make(cls, base[:-1] + [k]) for k, cls in zip((39, 40, 38, 39), (tuple, A, E, B))]
    s = 0
    for i in range(n):
        a, b = xs[i % 6], ys[i % 4]
        s += (a == b) + 2 * (a < b) + 4 * (b != a)
    return s


def lists(n):
    base = list(range(40))
    xs = [list(base), L(base), M(base), L(base[:-1]), base[:-1] + [99]]
    ys = [list(base), L(base), list(base)]
    s = 0
    for i in range(n):
        a, b = xs[i % 5], ys[i % 3]
        s += (a == b) + 2 * (a < b) + 4 * (b <= a)
    return s


def lookups(n):
    ks = [make((A, B, C, D, E, tuple)[i % 6], [i] * 39 + [-i]) for i in range(12)]
    d = {k: j for j, k in enumerate(ks)}
    # equal keys that are other objects: the dict's probe compares them
    probes = [make(type(k), list(k)) for k in ks]
    s = 0
    for i in range(n):
        s += d[probes[i % len(probes)]]
    return s


def odd_ones():
    t = make(A, range(40))
    r = tuple(range(40))
    out = [t == r, t != E(r), t < r[:-1] + (40,), t > (0, 1), t == list(r), t[39], t[-1]]
    out.append(len(M([1])))
    out.append(E((1, 2)) + (3,))
    out.append(tuple(L([4, 5])))
    try:
        (1, 2) < (1, 'x')
    except TypeError as e:
        out.append(str(e))
    return out


print(tuples(N))
print(lists(N))
print(lookups(600))
print(odd_ones())
