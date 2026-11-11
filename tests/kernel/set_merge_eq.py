"""Merges from a set or dict whose keys only __eq__ can judge: set.update,
union and dict.update with keys of a class with __eq__, a str subclass, a
Fraction equal to an int, and tuples holding such keys; an exception from
__eq__; a duplicate keyword from ** unpacking; and an __eq__ that changes the
dict being merged from."""
from fractions import Fraction


class K:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return hash(self.v)

    def __eq__(self, other):
        return isinstance(other, K) and other.v == self.v


class S(str):
    pass


s = {K(1)}
s.update({K(1)})
print('set.update K', len(s))
print('union K', len({K(1)} | {K(1)}))
print('union dict K', len({K(1)}.union(dict.fromkeys([K(1)]))))
s = {'a'}
s.update({S('a')})
print('set.update str subclass', len(s))
s = {1}
s.update({Fraction(1)})
print('set.update Fraction', len(s), sorted(map(type, s), key=str))
s = {(K(1),)}
s.update({(K(1),)})
print('set.update tuple K', len(s))
s = {K(1), 2, 'x'}
s.update({K(1): 0, 2.0: 0, 'x': 0, K(3): 0})
print('set.update mixed', len(s))

d = {K(1): 'a', 2: 'b'}
d.update({K(1): 'c', Fraction(2): 'd', 'e': 'f'})
print('dict.update', len(d), sorted(map(str, d.values())))
print('dict unpack', len({**{K(1): 1}, **{K(1): 2}}), list({**{1: 1}, **{Fraction(1): 2}}.values()))


class Raises(K):
    __hash__ = K.__hash__

    def __eq__(self, other):
        raise ValueError('eq')


for name, op in (('set.update', lambda: {Raises(1)}.update({Raises(1)})),
                 ('dict.update', lambda: {Raises(1): 1}.update({Raises(1): 2})),
                 ('set.union', lambda: {Raises(1)} | {Raises(1)})):
    try:
        op()
        print(name, 'no error')
    except ValueError as e:
        print(name, 'ValueError', e)


def f(**kw):
    return kw


try:
    f(**{'a': 1}, **{'a': 2})
except TypeError as e:
    print('duplicate keyword', e)


class Mutates(K):
    __hash__ = K.__hash__

    def __eq__(self, other):
        self.target['extra'] = 1
        return super().__eq__(other)


b = {K(1): 'b', 5: 5}
key = Mutates(1)
key.target = b
a = {key: 'a'}
try:
    a.update(b)
    print('mutated', 'no error')
except RuntimeError as e:
    print('mutated', e)
