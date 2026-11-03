# A weakref whose last reference went is dead even while a deep dealloc
# chain has queued its free: a collection meanwhile (here from a __del__)
# must not resurrect it to run its callback. Every nesting level is tried,
# so the weakref dies at whatever depth the dealloc chain starts queueing.
import gc
import weakref

gc.disable()


class Garbage:
    pass


class Collector:
    def __del__(self):
        gc.collect()


def callback(ref):
    pass


for level in range(1, 300):
    g = Garbage()
    g.me = g
    chain = None
    for depth in range(300, 0, -1):
        chain = [weakref.ref(g, callback) if depth == level else None, chain]
    del g
    outer = [Collector(), chain]     # the chain dies first, then the collection
    del chain, outer
gc.collect()
print("done")
