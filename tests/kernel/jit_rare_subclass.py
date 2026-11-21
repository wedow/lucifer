# luc-tax0: kernel code whose sites learned one exact class (the tuple
# comparison, entered from dict probes and from the generic path of a ==
# that meets many classes, on plain tuples) and then meet a tuple subclass
# now and then (a namedtuple, compared in turn on one probe in eight, or
# on one == in eight) fails its type guard there until the site gives the
# class up. The profile then knows both classes, and the sites read sizes
# and items in line under a guard on the kind they share instead of going
# generic. tools/jitcheck runs this at two sizes and requires that the
# helper calls compiled code makes do not grow with the length of what it
# compares: a few per comparison, not a few per item. What the
# comparisons and lookups answer must be the interpreter's.
import collections
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
WARM = 2000

ITEMS = 40
FIELDS = ' '.join(f'f{i}' for i in range(ITEMS))
P = collections.namedtuple('P', FIELDS)


def key(i):
    return tuple([i] * (ITEMS - 1) + [-i])


def lookups(n):
    # keys of ITEMS items, one a namedtuple; a probe is an equal tuple that
    # shares its items, but now and then holds an equal copy of the
    # namedtuple, which the comparison then compares in turn
    named = [P(*key(i)) for i in range(12)]
    keys = [tuple([i, named[i]] + list(range(ITEMS - 2))) for i in range(12)]
    d = {k: j for j, k in enumerate(keys)}
    same = [tuple(list(k)) for k in keys]
    copies = [tuple([i, P(*named[i])] + list(range(ITEMS - 2))) for i in range(12)]
    s = 0
    for i in range(n):
        k = i % 12
        s += d[copies[k] if i >= WARM and i % 8 == 0 else same[k]]
    return s


def compares(n):
    # a comparison whose operands' classes are more than its site takes in
    # line: tuples reach tuple.__eq__ through the kernel's generic path,
    # plain ones but for a namedtuple on one in eight
    a = key(5)
    plain = tuple(list(a))
    named = P(*a)
    others = [(1, 1), ('x', 'x'), (1.5, 1.5), (b'y', b'y'), (frozenset(), frozenset()), (None, None)]
    s = 0
    for i in range(n):
        x, y = others[i % 6]
        s += x == y
        b = named if i >= WARM and i % 8 == 0 else plain
        s += 2 * (b == a)
    return s


print(lookups(N))
print(compares(N))
print(P(*key(1)) == key(1), key(1) == P(*key(1)), P(*key(1)) < key(2), {key(3): 1}[P(*key(3))])
