# Instance dicts at tp_dictoffset (ast nodes, and a Python subclass of
# one) under threads: two threads make an instance's dict with their first
# stores, and each store lands in the one dict that wins; one thread
# replaces an instance's __dict__ while another reads, stores and deletes
# through it, and every read sees a whole dict; an old value's finalizer
# replaces the dict its store is replacing it in.
import ast
import threading


class Node(ast.AST):
    pass


for cls in (ast.AST, Node):
    made = [cls() for _ in range(3000)]
    start = threading.Barrier(2)

    def first_stores(name):
        start.wait()
        for o in made:
            setattr(o, name, name)

    threads = [threading.Thread(target=first_stores, args=(n,)) for n in ('left', 'right')]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print(cls.__name__, 'first stores', sum(sorted(vars(o)) == ['left', 'right'] for o in made))

    o = cls()
    o.a = 0
    stop = threading.Event()

    def replace_dicts():
        i = 0
        while not stop.is_set():
            i += 1
            o.__dict__ = {'a': i, 'b': -i}

    replacer = threading.Thread(target=replace_dicts)
    replacer.start()
    bad = 0
    try:
        for i in range(100000):
            if not isinstance(o.a, int):
                bad += 1
            o.c = i
            try:
                del o.c
            except AttributeError:
                pass            # the dict was replaced between the store and the delete
    finally:
        stop.set()
        replacer.join()
    print(cls.__name__, 'replaced under reads', bad, sorted(vars(o)))


# The value a store replaces is released after the store: its finalizer
# here replaces the dict the store went into.
class Swapper:
    def __init__(self, owner):
        self.owner = owner

    def __del__(self):
        self.owner.__dict__ = {'swapped': True}


for cls in (ast.AST, Node):
    hits = 0
    for i in range(2000):
        o = cls()
        o.v = Swapper(o)
        o.v = i
        hits += vars(o) == {'swapped': True}
    print(cls.__name__, 'finalizer swaps', hits)
