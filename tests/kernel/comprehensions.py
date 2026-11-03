# list, dict and set comprehensions: scope, nesting, conditions, closures, unpacking and errors.
print([x * x for x in range(6)], [x for x in range(10) if x % 2 if x > 3], [(x, y) for x in range(3) for y in range(x)])
print({x: x * x for x in range(5)}, {x % 3 for x in range(10)}, {(x, y): x + y for x in range(2) for y in range(2)})
print({k: v for k, v in [('a', 1), ('b', 2)]}, {v: k for k, v in {'a': 1, 'b': 2}.items()}, [k for k in {'p': 1, 'q': 2}])
print({x: [y for y in range(x)] for x in range(4)}, [{y for y in range(x)} for x in range(4)], [[j for j in range(i)] for i in range(4)])
print({x: x for x in range(3) if x}, {x for x in range(10) if x > 100}, {x: 1 for x in []}, [x for x in {}], {x for x in ()})
print({x: y for x in range(3) for y in range(x)}, {x // 2 for x in range(10)}, {len('x' * x): x for x in range(4)})

# The loop variable does not leak; names around it are untouched.
x = 'outer'
y = 'untouched'
print([x for x in range(3)], x, {x: x for x in range(2)}, x, {x for x in 'ab'} == {'a', 'b'}, x, y)
def scope():
    total = 100
    result = [total + i for i in range(3)]
    mapping = {i: total * i for i in range(3)}
    return result, mapping, total
print(scope())
def closures():
    fs = [lambda i=i: i for i in range(3)]
    gs = {i: (lambda: i) for i in range(3)}
    return [f() for f in fs], [gs[i]() for i in range(3)]
print(closures())
def uses_args(n, k):
    return {i: [j * k for j in range(n)] for i in range(n)}
print(uses_args(2, 3))
class Body:
    base = 10
    values = [base for _ in range(2)] if False else None
    table = {i: i for i in range(3)}
print(Body.table, Body.values)
print([i for i in range(3)], [x for x in [1, 2, 3] if x != (y := 2)], y)

# Unpacking, conditions with several clauses and side effects in order.
log = []
def tap(v):
    log.append(v)
    return v
print([tap(x) for x in range(3) if tap(x) > 0], log)
log.clear()
d = {tap(k): tap(v) for k, v in [(1, 2), (3, 4)]}
print(d, log)
log.clear()
s = {tap(x) for x in [3, 1, 3]}
print(s, log)
print([a + b for a, b in [(1, 2), (3, 4)]], [a for a, *rest in [(1, 2, 3), (4,)]], {a: rest for a, *rest in [(1, 2, 3), (4,)]})
print([(i, j) for i in range(3) if i for j in range(i + 1) if j != 1])
data = [{'n': 1, 'v': [1, 2]}, {'n': 2, 'v': [3]}]
print({r['n']: len(r['v']) for r in data}, [v for r in data for v in r['v']])

# Large comprehensions exercise resizing.
big = {i: i * i for i in range(2000)}
print(len(big), big[1999], len({i % 100 for i in range(5000)}), len([i for i in big if i % 3 == 0]))
words = {w: len(w) for w in ['alpha', 'be', 'gamma', 'be']}
print(words, len(words))

# Errors inside comprehensions.
for f in (lambda: [1 // x for x in range(2)], lambda: {x: 1 // x for x in [1, 0]}, lambda: {[]: x for x in range(1)},
          lambda: {[] for x in range(1)}, lambda: {x: y for x, y in [(1, 2, 3)]}, lambda: [x for x in 5],
          lambda: {k: v for k, v in [1]}, lambda: {x for x in range(3) if x.nope}):
    try:
        f()
    except (ZeroDivisionError, TypeError, ValueError, AttributeError) as exc:
        print(type(exc).__name__, exc)
def raises():
    try:
        return [x for x in [1, 2] if undefined_name]
    except NameError as exc:
        return str(exc)
print(raises())
def partial():
    out = []
    try:
        {out.append(x) or (1 // (2 - x)): x for x in range(5)}
    except ZeroDivisionError:
        pass
    return out
print(partial())

# Literals with several items, unpacking and duplicates.
print({1: 'a', 1: 'b'}, {1, 1, 1.0, True}, {**{'a': 1}, 'b': 2, **{'a': 3}}, {*[1, 2], *(3,), *{4}}, {*()})
print({'a': 1, **{}, **{'b': 2}}, [*{1: 2}, *{3: 4}.values()], {1: 1, 2.0: 2, 3: 3, (4,): 4, None: 5})
print({"a": 1, "b": {"c": [1, 2, {"d": None}]}}, {1, (2, 3)}, {frozenset([1]): {2}}, {i: i for i in range(3)} == {0: 0, 1: 1, 2: 2})
def kw(**kwargs):
    return kwargs
print(kw(**{'a': 1}), kw(**{'a': 1}, b=2), kw(a=1, **{'b': 2}), kw(**kw(x=1)), kw(**{}))
try:
    kw(**{1: 2})
except TypeError as exc:
    print(exc)
try:
    kw(a=1, **{'a': 2})
except TypeError as exc:
    print(exc)
try:
    kw(**5)
except TypeError as exc:
    print(exc)
class MapLike:
    def keys(self): return ['k']
    def __getitem__(self, key): return 'v'
print({**MapLike()}, kw(**MapLike()))
try:
    {**[1]}
except TypeError as exc:
    print(exc)
try:
    {**{1: 2}, **5}
except TypeError as exc:
    print(exc)
