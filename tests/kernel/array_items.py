# array item access: every typecode, negative and out-of-range indexes, and
# the conversions and errors of storing an item.
from array import array

for code in 'bBhHiIlLqQfd':
    a = array(code, [1, 2, 3])
    a[0] = 7
    a[-1] = a[1] + 1
    print(code, a[0], a[1], a[-1], a.tolist())
    for index in (3, -4, 2**70):
        try:
            a[index]
        except IndexError as e:
            print(' get', index, e)
        try:
            a[index] = 1
        except IndexError as e:
            print(' set', index, e)
    for value in (-1, 2**64, 1.5, 'x', True, 2**31):
        try:
            a[1] = value
            print(' stored', value, a[1])
        except (TypeError, OverflowError) as e:
            print(' refused', repr(value), type(e).__name__, e)
f = array('f', [0.0])
f[0] = 1e300
print(f[0], array('d', [0])[0])
w = array('w', 'ab')
w[0] = 'z'
print(w[0], w[-1], w.tolist())


class Index:
    def __index__(self):
        return 1


print(array('i', [5, 6])[Index()])
