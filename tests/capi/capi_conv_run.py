import os, sys, tempfile
import capi_conv as c


class Idx:
    def __index__(self):
        return 7


def show(f, *args):
    try:
        print(repr(f(*args)))
    except Exception as e:
        print(type(e).__name__, e)


# one-unit argument formats: masks, ranges and the errors the converters raise
for fmt, values in [('b', (255, 256, -1, 1.5)), ('B', (256, -1, 2**70)), ('H', (2**16 + 3, -1, 2**70)),
                    ('h', (2**15,)), ('i', (2**31, -2**31 - 1, 1.5, '1')),
                    ('I', (2**32 + 5, -1)), ('l', (1.5, 2**64)), ('L', (2**64,)),
                    ('k', (Idx(), -1, 1.5)), ('K', (Idx(), 2**64 + 1)), ('n', (Idx(), 'x')),
                    ('d', ('x', Idx(), 2)), ('f', ('x',))]:
    for v in values:
        for f in (fmt, fmt + ':conv'):
            show(c.parse, f, (v,))

# C int conversions
for kind, values in [('ulonglong', (-1, 2**64, 'x')), ('size_t', (-1, 2**64)),
                     ('uint32', (-1, 2**32, Idx())), ('double', ('x', 3))]:
    for v in values:
        show(c.to_c, kind, v)


# the dict lookups read the dict itself
class Missing(dict):
    def __missing__(self, key):
        return ['made', key]

    def __getitem__(self, key):
        return ['got', key]


d = Missing(a=1)
show(c.dict_get, d, 'a')
show(c.dict_get, d, 'b')

# types from specifications
print(c.Box.__doc__, c.tp_name(c.Box), c.Box.__module__, c.Box.__qualname__)
b = c.Box()
print(b.value, b.peek(), type(b).__name__)
show(c.Sealed)
print(c.tp_name(c.Pair))
p = c.Pair(range(100))
print(len(p), p[99], sum(p))

# a single-phase module inside a package takes the package-qualified name
with tempfile.TemporaryDirectory() as root:
    pkg = os.path.join(root, 'spkg')
    os.mkdir(pkg)
    open(os.path.join(pkg, '__init__.py'), 'w').close()
    os.symlink(c.__file__, os.path.join(pkg, 'single' + c.__file__[c.__file__.index('.'):]))
    sys.path.insert(0, root)
    import spkg.single as s
    print(s.__name__, s.__doc__, sys.modules['spkg.single'] is s)
