"""Methods read as attributes and called later (f = x.append, n.__index__,
s.upper) on instances without a dict, at sites that meet one and several
types, in hot loops; their identity and repr-relevant attributes; a method
of a class whose instances have a dict, and one shadowed by the instance."""
from support import loops


class Slotted:
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


class WithDict:
    def get(self):
        return 'class'


def run():
    out = []
    items = [3, 'ab', [1], Slotted(7), b'xy']
    acc = []
    for i in range(400):
        add = acc.append
        add(i)
        idx = (i).__index__
        out_len = len(acc)
        x = items[i % 5]
        m = x.__repr__ if i % 2 else x.__class__
        out.append((idx(), out_len, type(m).__name__))
    s = Slotted(5)
    g = s.get
    w = WithDict()
    w.get = lambda: 'instance'
    res = [len(out), out[:6], g(), g.__self__ is s, g.__func__ is Slotted.get, w.get(), WithDict().get(),
           'abc'.upper.__self__, [].append.__name__, (5).__index__.__qualname__]
    return res


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
