# A reader thread loops over a shared list of instances of a class with
# many names, loading and storing its highest-index names, while a writer
# thread gives the class small-room instances of another class by
# __class__ assignment and appends them. The reader's code may have been
# built before the class received one: it must find their values in the
# dict, never read or write past their own words, and the neighbours of
# every small instance must keep their values.
import sys
import threading

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 20
K = 40
NEIGHBOUR = 'neighbour'

READER = '''
def read(items, n):
    s = 0
    for r in range(n):
        for o in items:
            try:
                s += o.a39 + o.a38
            except (AttributeError, TypeError):
                pass
            o.a39 = 39
            o.a37 = r
    return s
'''


def round_():
    ns = {}
    exec("class B:\n    def __init__(self):\n" + "".join("        self.a%d = %d\n" % (i, i) for i in range(K)), ns)
    B = ns['B']

    class A:
        pass

    def make_a():
        o = A()
        o.x = NEIGHBOUR
        o.y = NEIGHBOUR
        return o

    for _ in range(60):
        A()
    items = [B() for _ in range(20)]
    exec(READER, ns)
    neighbours = []
    go = threading.Event()

    def writer():
        go.wait()
        for _ in range(30):
            neighbours.extend(make_a() for _ in range(5))
            a = make_a()
            neighbours.extend(make_a() for _ in range(5))
            a.__class__ = B
            items.append(a)

    t = threading.Thread(target=writer)
    t.start()
    go.set()
    total = ns['read'](items, 200)
    t.join()
    ns['read'](items, 2)
    ok = all(o.x is NEIGHBOUR and o.y is NEIGHBOUR and type(o) is A for o in neighbours)
    moved = all(o.a39 == 39 and o.a37 == 1 for o in items)
    return ok, moved, total > 0


print([round_() for _ in range(ROUNDS)] == [(True, True, True)] * ROUNDS)
