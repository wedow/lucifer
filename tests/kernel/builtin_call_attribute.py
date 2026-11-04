import math

print(len.__call__([1, 2, 3]), math.sqrt.__call__(16.0), [].append.__call__(1))
print(int.__add__.__call__(2, 3), (4).__add__.__call__(5), list.append.__call__([], 7))
print(dict.fromkeys.__call__('ab', 0), str.join.__call__(',', ['a', 'b']))
print(max.__call__(1, 5, key=lambda v: -v))
for f in (len, int.__add__, (4).__add__, list.append, dict.fromkeys):
    print(callable(f.__call__), hasattr(f, '__call__'))
try:
    len.__call__()
except TypeError as e:
    print(e)
