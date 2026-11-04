# keyword arguments named like the first parameter of __new__ and __init__
class Meta:
    def __init__(self, definition, cls=None, module=None):
        self.cls = cls
        self.module = module


m = Meta(1, cls='c', module='m')
print(m.cls, m.module)


class D(dict):
    pass


class L(list):
    pass


class O:
    def __init__(self, *args, **kwargs):
        self.args, self.kwargs = args, kwargs


print(O(cls=1).kwargs, sorted(dict(self=1, cls=2).items()), D(self=1, cls=2))
for t in (object, int, float, str, bytes, tuple, list, dict, set, frozenset, complex, slice, range, type):
    try:
        t(cls=1)
    except TypeError as e:
        print(t.__name__, e)
try:
    object.__new__(object, cls=1)
except TypeError as e:
    print(e)
