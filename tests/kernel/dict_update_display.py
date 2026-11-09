# {**mapping, key: value} (DICT_UPDATE): str and other keys, mappings that
# are not dicts, and the errors, in a function called often enough to compile.
import collections

class M:
    def keys(self):
        return ['k', 2]
    def __getitem__(self, key):
        return key * 2

def merge(a, b, k, v):
    return {**a, k: v, **b}

out = []
for i in range(300):
    out.append(merge({'x': i}, {'y': 1}, 'z', i))
    out.append(merge({1: 'one', (2, 3): i}, collections.OrderedDict(q=i), 4.5, None))
    out.append(merge(M(), {}, 'k', 'last'))
print(out[0], out[1], out[2], out[-3], len(out))
for bad in (1, [1, 2], None):
    try:
        merge({}, bad, 'a', 1)
    except TypeError as e:
        print('TypeError', e)
try:
    merge({}, {[1]: 2} if False else {'a': 1}, [], 1)
except TypeError as e:
    print('TypeError', e)
