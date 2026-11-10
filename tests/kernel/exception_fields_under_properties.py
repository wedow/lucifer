# An exception's __init__ stores its fields in the instance's layout, as
# CPython's C inits do, so a subclass may shadow one with a read-only
# property (importlib.metadata's PackageNotFoundError has a name property).


def show(make):
    try:
        e = make()
        print(type(e).__name__, 'made', e.args)
        return e
    except Exception as x:
        print(type(e).__name__ if False else 'error', type(x).__name__, x)


class NotFound(ModuleNotFoundError):
    @property
    def name(self):
        (name,) = self.args
        return name


e = show(lambda: NotFound('pkg'))
print(e.name, e.msg, e.path, str(e))
try:
    raise NotFound('other')
except ImportError as x:
    print('raised', x.name, x.msg)


def readonly(base, attr):
    return type('Shadow' + base.__name__, (base,), {attr: property(lambda self: 'property')})


cases = [
    (ImportError, 'msg', ('m',), {}), (ImportError, 'path', ('m',), {'path': 'p'}),
    (ImportError, 'name_from', ('m',), {}),
    (SystemExit, 'code', (3,), {}), (StopIteration, 'value', (4,), {}),
    (SyntaxError, 'msg', ('bad', ('f.py', 1, 2, 'x', 1, 3)), {}),
    (SyntaxError, 'filename', ('bad', ('f.py', 1, 2, 'x')), {}),
    (SyntaxError, 'text', ('bad', ('f.py', 1, 2, 'x')), {}),
    (SyntaxError, 'end_offset', ('bad', ('f.py', 1, 2, 'x', 1, 3)), {}),
    (OSError, 'errno', (2, 'gone'), {}), (OSError, 'strerror', (2, 'gone', 'f'), {}),
    (OSError, 'filename', (2, 'gone', 'f'), {}), (OSError, 'filename2', (2, 'gone', 'f', None, 'g'), {}),
    (UnicodeDecodeError, 'encoding', ('utf-8', b'\xff', 0, 1, 'bad'), {}),
    (UnicodeDecodeError, 'object', ('utf-8', b'\xff', 0, 1, 'bad'), {}),
    (UnicodeDecodeError, 'reason', ('utf-8', b'\xff', 0, 1, 'bad'), {}),
    (NameError, 'name', ('n',), {'name': 'x'}), (AttributeError, 'obj', ('a',), {'obj': 1}),
    (AttributeError, 'name', ('a',), {'name': 'y'}),
]
for base, attr, args, kwds in cases:
    cls = readonly(base, attr)
    try:
        e = cls(*args, **kwds)
        print(base.__name__, attr, 'made', getattr(e, attr), str(e))
    except Exception as x:
        print(base.__name__, attr, type(x).__name__, x)
