from collections import namedtuple

P = namedtuple('P', 'x y')
p = P(1, y=2)
print(p, p.x, p.y, p._asdict(), p._replace(x=5), P._make([3, 4]), P._fields, tuple(p))
print(P.__new__.__doc__, P.__name__, P.__module__)
D = namedtuple('D', ['a', 'b', 'c'], defaults=[10, 20])
print(D(1), D(1, 2), D(1, c=7), D._field_defaults)
S = namedtuple('S', 'a')
print(S(1), S(a=2), len(S(3)))
Z = namedtuple('Z', [])
print(Z())
R = namedtuple('R', 'a a _b', rename=True)
print(R._fields)
for args, kwargs in (((1, 2, 3), {}), ((), {}), ((1,), {}), ((1,), {'x': 1}), ((), {'x': 1, 'z': 3}), ((1, 2), {'x': 3})):
    try:
        P(*args, **kwargs)
    except TypeError as e:
        print(e)
for args in ((), (1, 2, 3, 4, 5)):
    try:
        D(*args)
    except TypeError as e:
        print(e)
try:
    Z(1)
except TypeError as e:
    print(e)
try:
    namedtuple('Bad', 'class')
except ValueError as e:
    print(e)
print(P(1, 2) == (1, 2), hash(P(1, 2)) == hash((1, 2)), P(1, [2]).__class__.__mro__[1])
