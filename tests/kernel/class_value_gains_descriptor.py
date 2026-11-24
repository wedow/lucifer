# A plain value sitting as a class attribute whose own class gains, and then
# loses, a descriptor method late in a hot loop: __get__ (a non-data
# descriptor), __get__ with __set__ (a data descriptor, which wins over an
# instance's own entry), __set__ alone and __delete__ alone (data
# descriptors with no __get__: an instance's own entry wins, else the value
# itself). Read through an instance with and without its own entry, through
# the class, through super() and through getattr. Compiled code that took
# the value as a constant must see each change at once. Each result must be
# the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class K:
    pass


class H:
    x = K()


class S(H):
    def get(self):
        return super().x


def show(v):
    return 'K' if type(v) is K else v


def inst(o, p):
    return o.x


def own(o, p):
    return p.x


def cls(o, p):
    return H.x


def sup(o, p):
    return o.get()


def via_getattr(o, p):
    return getattr(o, 'x')


def own_getattr(o, p):
    return getattr(p, 'x')


GAINS = {
    'get': {'__get__': lambda s, obj, typ: 'get'},
    'data': {'__get__': lambda s, obj, typ: 'dget', '__set__': lambda s, obj, v: None},
    'set': {'__set__': lambda s, obj, v: None},
    'delete': {'__delete__': lambda s, obj: None},
}


def gain(names):
    for name, f in names.items():
        setattr(K, name, f)


def lose(names):
    for name in names:
        delattr(K, name)


def run(read, names, n):
    o = S()
    p = S()
    p.__dict__['x'] = 'own'
    out = []
    for change in (gain, lose):
        r = None
        for i in range(n):
            if i == n - 50:
                change(names)
            r = read(o, p)
        out.append(show(r))
    return out


for read in (inst, own, cls, sup, via_getattr, own_getattr):
    print(read.__name__, [(kind, run(read, names, N)) for kind, names in GAINS.items()])
