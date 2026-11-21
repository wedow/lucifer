import pickle
from functools import Placeholder, partial, partialmethod


def collect(*args, **kwargs):
    return args, sorted(kwargs.items())


inner = partial(collect, Placeholder, 2, color="red")
inner.label = "inner"
outer = partial(inner, 1, color="blue")
print(outer.func is inner, outer.args, outer.keywords)
print(outer(3))

inner.__dict__.clear()
outer = partial(inner, 1, color="blue")
print(outer.func is inner, outer.args, outer.keywords)
print(outer(3))

fresh = partial(collect, Placeholder, 2, color="red")
flat = partial(fresh, 1, color="blue")
print(flat.func is collect, flat.args, flat.keywords)
print(flat(3))

print(fresh.__dict__)
print(partial(fresh, 1).func is fresh)
print(fresh.__reduce__()[2][-1] is fresh.__dict__)
restored = pickle.loads(pickle.dumps(fresh))
print(type(restored) is partial, restored.args[0] is Placeholder)
print(partial(restored, 1).func is restored)

deleted = partial(collect, 1)
deleted.label = "deleted"
del deleted.label
print(partial(deleted, 2).func is deleted)

deleted.__setstate__((collect, (1,), None, None))
print(partial(deleted, 2).func is collect)

untouched = partial(collect, 1)
print(untouched.__reduce__()[2][-1])
print(partial(untouched, 2).func is collect)
object.__getstate__(untouched)
print(partial(untouched, 2).func is collect)


class Example:
    def method(self, *args):
        return args

    first = partialmethod(method, 1)
    first.label = "first"
    second = partialmethod(first, 2)


print(Example.second(Example(), 3))

for state in ([collect, (), None, None], (collect, (), None), (1, (), None, None)):
    try:
        partial(collect).__setstate__(state)
    except TypeError as e:
        print(e)
