class Name(str):
    pass


def f(a, **kw):
    return a, sorted((k, v) for k, v in kw.items())


def g(a, b=0):
    return a, b


print(f(1, **{Name('x'): 2}))
print(g(1, **{Name('b'): 5}))
print(dict(**{Name('k'): 1}))
try:
    g(1, **{Name('zz'): 5})
except TypeError as e:
    print(e)
try:
    g(1, **{Name('b'): 5}, b=6)
except TypeError as e:
    print(e)
try:
    g(1, **{1: 5})
except TypeError as e:
    print(e)


class Hashed(str):
    """A key the dict finds only as itself: its hash is not the str's."""
    def __hash__(self):
        return 1


print(f(1, **{Hashed('x'): 2}), g(1, **{Hashed('b'): 7}), dict(**{Hashed('k'): 3}))


# A parameter matches a keyword by identity, then by the keyword's __eq__, as
# initialize_locals compares; a builtin matches by text, as find_keyword does.
# A Python function's **kwargs keeps the caller's key objects.
class EqAll(str):
    def __eq__(self, other):
        return True

    def __hash__(self):
        return str.__hash__(self) ^ 3


class EqNone(str):
    def __eq__(self, other):
        return False

    def __hash__(self):
        return str.__hash__(self)


def h(a, b=0, *, c=1):
    return a, b, c


def keys(**kw):
    return [(type(k).__name__, k, v) for k, v in kw.items()]


def show(call):
    try:
        r = call()
    except TypeError as e:
        r = 'TypeError: %s' % e
    print(r)


for K in (str, Name, EqAll, EqNone):
    print(K.__name__)
    show(lambda: h(1, **{K('b'): 5}))
    show(lambda: h(1, **{K('c'): 5}))
    show(lambda: h(1, **{K('zz'): 5}))
    show(lambda: keys(**{K('x'): 2, 'y': 3}))
    show(lambda: f(1, **{K('a'): 2}))
    show(lambda: h(1, **{K('b'): 5}, b=6))
    show(lambda: h(1, b=6, **{K('b'): 5}))
    show(lambda: h(1, **{K('b'): 5}, **{'b': 6}))
    show(lambda: sorted([1, 3, 2], **{K('reverse'): True}))
print(h(1, b=2, c=3), h(1, **{'c': 4}), keys(x=1, **{'y': 2}))
