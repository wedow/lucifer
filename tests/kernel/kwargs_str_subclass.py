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
