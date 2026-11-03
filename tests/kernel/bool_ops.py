# bool is an int with two instances and its own logic operators.
t = True
f = False
print(t, f, t + t, t + 1, f * 5, t - f, -t, +t, t // t, t % t)
print(t & t, t & f, t | f, f | f, t ^ t, t ^ f, f ^ f)
print(t & 1, t | 2, t ^ 3, 1 & t, 2 | t, 3 ^ t, t & 0, 0 | f)
print(t << 3, t >> 1, f << 3, 5 << t, 5 >> t)
print(t == 1, f == 0, t == t, t != f, t < 2, f < t, t > f, t >= t)
print(t is True, f is False, t is not f)
print(t.__repr__(), f.__str__(), t.__int__(), f.__index__(), t.__hash__(), f.__hash__())
print(t.__and__(f), t.__rand__(True), t.__or__(f), t.__ror__(f), t.__xor__(t), t.__rxor__(f))
print(t.__and__(1), t.__and__('x') is NotImplemented, t.__or__(1.5) is NotImplemented)
print(bool(), bool(0), bool(1), bool(-5), bool(10 ** 30), bool(0 * 10 ** 30), bool(None), bool(''), bool('a'))
print(bool(t), bool(f), bool(()), bool((0,)), bool([]), bool([0]), bool({}), bool({"a": 0}))
print(bool.__new__(bool, 5), bool.__new__(bool, 0))
print(t.bit_length(), f.bit_length(), t.bit_count(), t.real, t.imag, t.numerator, t.denominator)
print(type(t) is bool, type(t + t) is int, type(t & t) is bool, type(t & 1) is int, type(-t) is int)
print(type(t | f) is bool, type(t ^ t) is bool, type(f ^ f) is bool)
print(t.__class__ is bool, bool.__name__, int.__name__, bool.__base__ is int)
x = 3
print(not x, not 0, not t, not f, x and 5, 0 and 5, x or 5, 0 or 5, t and f, f or t)
print(1 if x else 0, 1 if 0 else 0)
n = 0
while n < 3 and t:
    n += 1
print(n)
print(isinstance(t, bool), isinstance(t, int), isinstance(1, bool), isinstance(f, object))
print(issubclass(bool, int), issubclass(int, bool))
class Box:
    def __contains__(self, x):
        return x - 5
box = Box()
print(5 in box, 7 in box, 5 not in box, 7 not in box)
class Truthy:
    def __bool__(self):
        return True
class Falsy:
    def __bool__(self):
        return False
class Sized:
    def __len__(self):
        return 3
class Empty:
    def __len__(self):
        return 0
class Plain:
    pass
print(bool(Truthy()), bool(Falsy()), bool(Sized()), bool(Empty()), bool(Plain()))
print(not Truthy(), not Falsy(), not Sized(), not Empty(), not Plain())
if Sized():
    print('sized')
if not Empty():
    print('empty')
