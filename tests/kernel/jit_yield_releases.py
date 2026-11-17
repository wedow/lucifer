# A generator yields a tuple made of what another generator yielded to it:
# the inner one, resumed in line, yields a tuple compiled code never makes,
# and the outer one makes it real only to yield it. The reference the
# unmade tuple held on its item is released at the yield: every object is
# freed once the program drops it.
import gc
import weakref


class Item:
    pass


def inner(items):
    for item in items:
        yield (item,)


def outer(job, pairs):
    for pair in pairs:
        yield (job, pair)


def alive_after(n):
    items = [Item() for _ in range(n)]
    refs = [weakref.ref(item) for item in items]
    for task in outer(1, inner(items)):
        pass
    del items, task
    gc.collect()
    return sum(ref() is not None for ref in refs)


print("alive", sum(alive_after(10) for _ in range(300)))
