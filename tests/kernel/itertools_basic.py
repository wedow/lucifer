import itertools
from itertools import *

def terr(f, *a, **k):
    try:
        f(*a, **k)
    except BaseException as e:
        print(type(e).__name__)
    else:
        print("no error")

def err(f, *a, **k):
    try:
        r = f(*a, **k)
    except BaseException as e:
        print(type(e).__name__, e)
    else:
        print("ok", r)

for t in (chain, repeat, starmap, islice, count, cycle, accumulate, compress, dropwhile, takewhile,
          filterfalse, zip_longest, pairwise, batched, product, permutations, combinations,
          combinations_with_replacement, groupby):
    print(t.__module__, t.__name__, t.__qualname__, type(t).__name__)

c = chain([1, 2], (3,), 'ab')
print(type(c).__name__, iter(c) is c, list(c), list(chain()), list(chain.from_iterable(['ab', 'c'])))
err(list, chain(1)); err(chain.from_iterable, 1); err(list, chain.from_iterable([1]))
print(chain.from_iterable.__self__.__name__)
terr(chain, a=1)
print(list(chain.from_iterable(iter([[1], [2]]))), chain.__class_getitem__(int) if hasattr(chain, '__class_getitem__') else '')

r = repeat('x', 3)
print(r, list(r), repr(repeat(1)), repr(repeat(1, 0)), repr(repeat(1, -2)), r.__length_hint__())
r = repeat(7, 2); next(r); print(r, r.__length_hint__(), len(list(r)))
terr(len, repeat(1)); err(operator_length_hint := repeat(1).__length_hint__)
print(list(repeat(1, -1)), list(repeat(object=2, times=2)))
terr(repeat); err(repeat, 1, 'a'); terr(repeat, 1, 2, 3)
r = repeat(1); print(next(r), next(r), r)

print(list(starmap(pow, [(2, 3), (3, 2)])), list(starmap(lambda *a: a, [])))
err(list, starmap(pow, [1])); terr(starmap, 1); err(list, starmap(1, [(1,)])); err(starmap, pow, 5)
print(list(starmap(max, iter([(1, 2)]))))

L = range(10)
print(list(islice(L, 3)), list(islice(L, 2, 5)), list(islice(L, 1, 8, 3)), list(islice(L, None)), list(islice(L, 5, None)),
      list(islice(L, 0)), list(islice(L, 3, 1)), list(islice(L, None, None, 4)), list(islice(L, 20)))
for a in ((-1,), (1, -1), (0, 1, 0), (0, 1, -1), ('a',), (1.5,), (1, 2, 3, 4), (), (0, 1, 'a'), (0, 1, None)):
    err(lambda a=a: list(islice(L, *a)))
it = iter(range(10)); list(islice(it, 3)); print(next(it))
it = iter(range(10)); print(list(islice(it, 2, 4)), next(it))
it = iter(range(10)); list(islice(it, 0)); print(next(it))
err(islice, 5, 1)
print(list(islice(count(), 4)), list(islice(L, None, 3, None)))
print(type(islice(L, 1)).__name__, iter(islice(L, 1)).__class__.__name__)

c = count(); print(c, next(c), next(c), c)
print(count(5), count(5, 2), count(1.5), count(2, 0.5), count(0, -1), count(step=3), count(start=2), repr(count(1, 1)))
c = count(10, -3); print([next(c) for _ in range(3)], c)
print(repr(count(2**70)), repr(count(1, 2**70)), repr(count(1.0, 2)), repr(count(1, 1.0)), repr(count(complex(1, 1))))
err(count, 'a'); err(count, 1, 'a'); terr(count, 1, 2, 3)
c = count(2**62); next(c); print(next(c), next(c) > 2**62)
c = count(3); print([next(c) for _ in range(3)])
class N(int): pass
print(next(count(N(3))), count(True), next(count(True)))

cy = cycle('abc'); print([next(cy) for _ in range(7)], type(cy).__name__, iter(cy) is cy)
print(list(cycle([])), list(islice(cycle(iter([1, 2])), 5)))
err(cycle, 1); terr(cycle)

print(list(accumulate([1, 2, 3, 4])), list(accumulate([1, 2, 3], lambda a, b: a * b)), list(accumulate([])), list(accumulate([1, 2], initial=10)),
      list(accumulate([], initial=0)), list(accumulate('abc')), list(accumulate([1, 2, 3], initial=None)), list(accumulate([3, 1, 2], max)))
err(list, accumulate([1, 'a'])); terr(accumulate); err(accumulate, 1); err(list, accumulate([1, 2], 5))
print(type(accumulate([])).__name__)

print(list(compress('abcdef', [1, 0, 1, 0, 1, 1])), list(compress('abc', [1])), list(compress([], [1])), list(compress('ab', [])))
err(compress, 1, 2); terr(compress, [1]); err(compress, [1], 1)

print(list(dropwhile(lambda x: x < 3, [1, 2, 3, 1, 5])), list(takewhile(lambda x: x < 3, [1, 2, 3, 1, 5])), list(dropwhile(bool, [])), list(takewhile(None, [1, 0, 1]) if False else []))
terr(dropwhile, 1); err(takewhile, 1, 2); err(list, takewhile(1, [1])); err(list, dropwhile(1, [1]))
it = iter([1, 2, 3, 4]); list(takewhile(lambda x: x < 2, it)); print(next(it))
print(list(filterfalse(lambda x: x % 2, range(8))), list(filterfalse(None, [0, 1, '', 'a', None])))
terr(filterfalse, 1); err(list, filterfalse(1, [1])); terr(filterfalse)

print(list(zip_longest('ab', 'xyz')), list(zip_longest('ab', 'xyz', fillvalue='-')), list(zip_longest()), list(zip_longest([], [])), list(zip_longest('a', fillvalue=0)))
err(zip_longest, 'a', 1); terr(zip_longest, 'a', foo=1)
print(list(zip_longest([1], [2, 3], [4, 5, 6], fillvalue=None)), list(zip_longest(range(2), iter('abc'))))

print(list(pairwise('abcd')), list(pairwise('a')), list(pairwise([])), list(pairwise(iter([1, 2, 3]))))
err(pairwise, 1); terr(pairwise)

print(list(batched('abcdefg', 3)), list(batched('abc', 3)), list(batched('', 2)), list(batched('abcd', 2, strict=True)), list(batched([1, 2], 1)))
err(list, batched('abc', 2, strict=True)); err(batched, 'abc', 0); err(batched, 'abc', -1); err(batched, 1, 2); terr(batched, 'abc'); err(batched, 'abc', 'a')
print(type(next(batched('abc', 2))).__name__)

print(list(product('ab', range(2))), list(product()), list(product('ab', repeat=2)), list(product([], 'a')), list(product(range(2), repeat=0)), list(product('a', repeat=3)))
err(product, 1); err(product, repeat=-1); terr(product, 'a', foo=1); err(product, 'a', repeat='x')
print(list(product(iter('ab'), iter('xy'))))
print(list(permutations('abc')), list(permutations('abc', 2)), list(permutations('ab', 3)), list(permutations('abc', 0)), list(permutations([])), list(permutations(range(3), None)))
err(permutations, 1); err(permutations, 'ab', -1); err(permutations, 'ab', 'a'); terr(permutations)
print(list(combinations('abcd', 2)), list(combinations('abc', 0)), list(combinations('ab', 3)), list(combinations('', 0)), list(combinations(range(4), 4)))
err(combinations, 'ab', -1); err(combinations, 1, 1); terr(combinations, 'ab')
print(list(combinations_with_replacement('abc', 2)), list(combinations_with_replacement('ab', 0)), list(combinations_with_replacement('', 2)), list(combinations_with_replacement('ab', 3)))
err(combinations_with_replacement, 'ab', -1); err(combinations_with_replacement, 1, 1)

print([(k, list(g)) for k, g in groupby('aabbbc')], [(k, list(g)) for k, g in groupby([1, 2, 3, 4, 5], lambda x: x // 2)], list(groupby([])),
      [k for k, g in groupby('AaBb', str.upper)], [k for k, g in groupby([0, 0, 1], None)])
gb = groupby('aabb'); k1, g1 = next(gb); k2, g2 = next(gb); print(k1, k2, list(g1), list(g2))
gb = groupby('aabb'); k1, g1 = next(gb); print(list(g1), list(g1)); print(type(g1).__name__)
gb = groupby('aab'); k1, g1 = next(gb); print(next(g1)); print(next(gb)[0]); err(next, g1)
terr(groupby); err(groupby, 1); err(list, groupby('ab', 1)); terr(groupby, 'a', None, 3)
print(type(gb).__name__, iter(gb) is gb)

a, b = tee('abc'); print(list(a), list(b)); print(type(a).__name__)
a, b, c = tee(iter(range(3)), 3); print(next(a), list(b), list(c), list(a))
print(tee('ab', 0), len(tee('ab')), len(tee('ab', 1)))
err(tee, 1); err(tee, 'ab', -1); err(tee, 'ab', 'a'); terr(tee)
a, b = tee(iter(range(5))); next(a); next(a); print(list(b), list(a))
it = iter(range(3)); t = tee(it); print(t[0] is iter(t[0]))
a, b = tee([]); print(list(a), list(b))

# composition
print(sum(1 for _ in islice(count(), 1000)), list(chain.from_iterable(zip('ab', 'cd'))), dict(zip_longest('ab', [1])))
print(list(map(sum, batched(range(10), 3))), [''.join(p) for p in permutations('abc', 2)][:3])
print(list(takewhile(lambda x: x < 40, accumulate(count(1)))))
class MyChain(chain): pass
print(list(MyChain([1], [2])), MyChain.__module__)
class MyRepeat(repeat): pass
print(list(MyRepeat(1, 2)))
