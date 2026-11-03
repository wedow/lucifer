# Operator dispatch around int: reflection, subclass priority, in-place
# forms, rich comparison fallbacks and int subclasses.
class V:
    def __init__(self, n):
        self.n = n
    def __add__(self, other):
        print('V.add')
        return V(self.n + (other.n if type(other) is V else other))
    def __radd__(self, other):
        print('V.radd')
        return V(other + self.n)
    def __sub__(self, other):
        return NotImplemented
    def __rsub__(self, other):
        print('V.rsub')
        return 'rsub'
    def __iadd__(self, other):
        print('V.iadd')
        self.n += other
        return self
    def __lt__(self, other):
        print('V.lt')
        return self.n < other
    def __gt__(self, other):
        print('V.gt')
        return self.n > other
    def __eq__(self, other):
        print('V.eq')
        return self.n == other
    def __neg__(self):
        return V(-self.n)
    def __invert__(self):
        return 'inv'
    def __pos__(self):
        return 'pos'
    def __pow__(self, other, mod=None):
        return other * 100 + (0 if mod is None else mod)
    def __rpow__(self, other, mod=None):
        return other * 1000
    def __bool__(self):
        return self.n != 0
    def __contains__(self, item):
        return item == self.n

v = V(10)
print((v + 5).n, (5 + v).n, (v + v).n)
print(5 - v)
print(v < 20, v > 20, 20 > v, 20 < v, v == 10, 10 == v, v != 10)
print((-v).n, ~v, +v)
print(v ** 2, 2 ** v)
print(v.__pow__(2, 3))
w = v
w += 5
print(w is v, v.n)
x = 5
x += 1
print(x)
print(10 in v, 11 in v, 10 not in v)
print(bool(V(0)), bool(v), not V(0))

class Meters(int):
    def __radd__(self, other):
        print('Meters.radd')
        return 'meters'

m = Meters(7)
print(m.__repr__(), type(m + 1) is int, m + 1, 1 + m, type(m) is Meters)
print(int(m), type(int(m)) is int, m == 7, m < 8, m.__hash__(), bool(Meters(0)), bool(m))
print(m.real, type(m.real) is int, m.numerator, m.denominator, m.bit_length())
print(Meters(2 ** 100).__repr__(), Meters('12').__repr__(), Meters('ff', 16).__repr__(), Meters(True).__repr__())
print(Meters.from_bytes(b'\x01\x00', 'big').__repr__())
print(type(-m) is int, type(m * 2) is int, type(m // 2) is int)
print(m.__add__(1), m.__radd__(1), int.__radd__(m, 1))

class Idx:
    def __index__(self):
        return 3
    def __int__(self):
        return 9
class JustInt:
    def __int__(self):
        return 11
class BadInt:
    def __int__(self):
        return 'x'
print(int(Idx()), int(JustInt()))
print(int.from_bytes((5).to_bytes(Idx(), 'big'), 'big') == 5, (1234).__round__(Idx()))
print(int(' 0x1F ', 16), int('-0b11', 0), int('+0o17', 0), int('1e', 16), int('Z', 36), int(b'  12  '))
print(int(True) + int(False))
print(int('0'), int('000'), int('0_0'), int('-0'), int('+7'))
print(int('  \t\n12\x0b\x0c\r '), int('1 ') if False else 1)
print(0 == False, 1 == True, 2 > True, False < 1, True + True == 2)
