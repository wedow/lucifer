"""Native answers the tier relies on: property loads (a re-initialized
property, a getter that raises, a subclass's override), methods and slot
wrappers bound on instances, bytearray slices, len() and bytes() of
bytearrays and their subclasses, reversed bytes slices."""
from support import loops


class Base:
    def __init__(self, v):
        self._v = v

    @property
    def v(self):
        return self._v

    @property
    def bad(self):
        raise LookupError('no')


class Sub(Base):
    @property
    def v(self):
        return -self._v


class BA(bytearray):
    def __len__(self):
        return 99


def run():
    out = []
    objs = [Base(1), Sub(2), Base(3)]
    out.append(sum(o.v for o in objs for _ in range(50)))
    try:
        objs[0].bad
    except LookupError as e:
        out.append(str(e))
    p = Base.__dict__['v']
    saved = p.fget
    p.__init__(lambda self: 7)
    out.append(objs[0].v)
    p.__init__(saved)
    out.append(objs[0].v)
    n = 5
    out.append((n.__index__(), n.bit_length.__name__, (3.5).hex()))
    ba = bytearray(b'abcdefgh')
    out.append((ba[2:5], ba[::-1], ba[1::3], ba[-3:], ba[10:], len(ba), bytes(ba), type(bytes(ba))))
    sub = BA(b'xyz')
    out.append((len(sub), bytes(sub), type(sub[0:2])))
    out.append((b'hello'[::-1], b'hello'[1::2], b'hello'[4:1:-1]))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
