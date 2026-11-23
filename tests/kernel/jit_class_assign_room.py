# An instance that came to a class by __class__ assignment keeps the room
# its first class gave it, which can be far less than the new class's
# names: code compiled for the new class must find its values in the
# dict, never read or write past the instance's own words. The small
# instance sits among neighbours whose words all hold a value, so a read
# past its room finds one; each of the new class's names is read and
# written by code of its own.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
K = 40

ns = {}
exec("class B:\n    def __init__(self):\n" + "".join("        self.a%d = %d\n" % (i, i) for i in range(K)), ns)
B = ns['B']
for i in range(K):
    exec("def load%d(o):\n    return o.a%d\n" % (i, i), ns)
    exec("def load_i%d(o):\n    return o.a%d + 1\n" % (i, i), ns)
    exec("def store%d(o, v):\n    o.a%d = v\n" % (i, i), ns)
loads = [ns['load%d' % i] for i in range(K)]
loads_i = [ns['load_i%d' % i] for i in range(K)]
stores = [ns['store%d' % i] for i in range(K)]


class A:
    pass


NEIGHBOUR = 'neighbour'


def make_a():
    o = A()
    o.x = NEIGHBOUR
    o.y = NEIGHBOUR
    return o


bs = [B() for _ in range(10)]
for r in range(N // 100):
    for b in bs:
        for i in range(K):
            loads[i](b)
            loads_i[i](b)
            stores[i](b, i)
for _ in range(60):
    A()                                 # A's spare room is spent: later instances get little
before = [make_a() for _ in range(300)]
a = make_a()
after = [make_a() for _ in range(300)]
a.__class__ = B
found = []
for i in range(K):
    for f in (loads[i], loads_i[i]):
        try:
            found.append((i, f(a)))
        except AttributeError:
            pass
for i in range(K):
    stores[i](a, -i)
print(found)
print(sorted(vars(a).items()) == sorted([('x', NEIGHBOUR), ('y', NEIGHBOUR)] + [('a%d' % i, -i) for i in range(K)]))
print([loads[i](a) for i in range(0, K, 13)], [loads_i[i](a) for i in range(0, K, 13)])
print(all(o.x is NEIGHBOUR and o.y is NEIGHBOUR and type(o) is A for o in before + after))
print(sum(loads[i](b) + loads_i[i](b) for b in bs for i in range(K)))
