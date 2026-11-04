import operator


class Idx:
    def __index__(self):
        return 7


class BadIdx:
    def __index__(self):
        return 'x'


class Big(int):
    pass


for value in (3, True, Idx(), Big(5), -2**70):
    print(repr(operator.index(value)), type(operator.index(value)).__name__)
for bad in (1.5, 'a', None, range(3), [1], BadIdx()):
    try:
        operator.index(bad)
    except TypeError as e:
        print(type(e).__name__, e)
print([1, 2, 3][operator.index(Idx()) - 6], operator.index.__name__)
