# Slot wrappers (wrapper_descriptor) called unbound: from Python, from C
# (map), on a subclass instance, through the special-method lookups that
# reach C slots (`in`, `[]`), and the errors for a missing or wrong self.
class S(str):
    pass


fs = frozenset([1, 2, 3])
print(str.__getitem__('abc', 1), frozenset.__contains__(fs, 2), int.__add__(1, 2))
print(list(map(str.__getitem__, ['ab', 'cd'], [0, 1])))
print(list(map(frozenset.__contains__, [fs, fs], [3, 4])))
print(str.__getitem__(S('xyz'), -1), str.__len__('four'), dict.__contains__({1: 2}, 1))
lst = [0, 0]
list.__setitem__(lst, 1, 'set')
print(lst)
total = 0
for i in range(2000):
    total += (i % 5) in fs
    total += len(str.__getitem__('abcdef', i % 6))
print(total)
for call in (lambda: str.__getitem__(1, 0), lambda: str.__getitem__(),
             lambda: frozenset.__contains__(fs, []), lambda: str.__getitem__('ab', 5),
             lambda: list(map(str.__getitem__, [1], [0]))):
    try:
        call()
    except Exception as e:
        print(type(e).__name__, e)
