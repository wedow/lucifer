# float arithmetic, comparison, repr, parsing, rounding, hashing and formatting.
# Only operators, dunder calls, f-strings and print: the builtins arrive later.
inf = 1e308 * 10
nan = inf - inf
ninf = -inf

def show(*xs):
    print(*[f"{x!r}" for x in xs])

def err(f, *a):
    try:
        print(f(*a))
    except BaseException as e:
        print(type(e).__name__, e)

# repr: shortest round-trip
show(0.1, 0.2, 0.1 + 0.2, 1 / 3, 2 / 3, 1e16, 1e15, 123456789012345678.0, 1e-4, 1e-5)
show(1e22, 1e23, 5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, 0.0, -0.0)
show(inf, ninf, nan, -nan, 100.0, 1.5e300, 4.35, 2.675, 1e100, 12345.678)
print(0.1, str(1.5), 3.0)

# arithmetic and mixing with ints
a = 7.5
show(a + 1, 1 + a, a - 2, 2 - a, a * 3, 3 * a, a / 2, 3 / a, a // 2, a % 2, 2 // a, 2 % a)
show(-a // 2, -a % 2, a // -2, a % -2, -a // -2, -a % -2)
show(a ** 2, 2 ** a, a ** -1, a ** 0.5, 2.0 ** 0.5, 0.0 ** 0, 0.0 ** 2, (-2.0) ** 3, (-2.0) ** 2)
show((-8.0) ** (1 / 3), (-1.0) ** 0.5, inf ** 0, inf ** -1, ninf ** 3, ninf ** 2, 1.0 ** nan, nan ** 0)
show(True + 1.5, 1.5 * True, 5 / 2, 4 / 2, -5 / 2, 10 ** 20 / 3, 10 ** 400 / 10 ** 399)
show(a.__divmod__(2), a.__rdivmod__(2), (-a).__divmod__(2), a.__divmod__(-2), (0.5).__divmod__(0.25))
show(-0.0 // 1, 0.0 // -1, -0.0 % 1, 0.0 % -1, 5.0 % inf, -5.0 % inf, 5.0 % ninf, inf % 5)
show(-a, +a, a.__abs__(), (-a).__abs__(), a.__neg__(), a.__pos__())
x = 1.5
x += 1
x *= 2
x -= 0.5
x /= 4
x //= 0.25
x **= 2
show(x)

# errors
err(lambda: 1.0 / 0)
err(lambda: 1.0 / 0.0)
err(lambda: 1 / 0.0)
err(lambda: 1.0 // 0)
err(lambda: 1.0 % 0)
err(lambda: a.__divmod__(0.0))
err(lambda: 0.0 ** -1)
err(lambda: 10.0 ** 400)
err(lambda: 1e300 * 10 ** 10)
err(lambda: 2.0 ** 100000)
err(lambda: 1.5 + 10 ** 400)
err(lambda: 10 ** 400 + 1.5)
err(lambda: 1.5 + 'a')
err(lambda: 'a' + 1.5)
err(lambda: 1.5 < 'a')
err(lambda: 1.5 & 1)
err(lambda: 1.5 @ 1.5)
err(lambda: -'a')
err(lambda: a.__pow__(2, 3))
err(lambda: 10 ** 400 / 2.0)
err(lambda: 10 ** 400 // 2.0)

# comparison with floats, ints, nan, inf
show(1.0 < 2.0, 2.0 <= 2.0, 2.0 == 2.0, 2.0 != 2.0, 3.0 > 2.0, 2.0 >= 3.0)
show(nan == nan, nan != nan, nan < 1, nan > 1, nan <= nan, nan >= 1.0, 1 < nan, 1 == nan, 1 != nan)
show(inf > 10 ** 400, ninf < -10 ** 400, inf == 10 ** 400, 10 ** 400 < inf, 10 ** 400 > ninf)
show(inf == inf, inf > ninf, ninf < 0, 0 < inf, -0.0 == 0.0, -0.0 == 0, 0 == -0.0)
big = 2 ** 53
show(float(big) == big, float(big) == big + 1, float(big) < big + 1, float(big) > big - 1, big + 1 > float(big))
show(float(big + 2) == big + 2, float(big + 2) < big + 3, float(big + 2) > big + 1)
big = 2 ** 100
show(float(big) == big, float(big) == big + 1, float(big) < big + 1, float(big) > big - 1, big + 1 > float(big))
show(float(big) == -big, -float(big) == -big, -float(big) < -big + 1, -float(big) > -big - 1)
show(1e300 > 10 ** 300, 1e300 < 10 ** 300 + 10 ** 290, 1e300 == 10 ** 300)
show(0.5 < 1, 0.5 > 0, 0.5 == 0, 1.5 > 1, 1.5 < 2, 1.0 == 1, 1.0 == 2, -1.5 < -1, -1.5 > -2, 0.5 < True)
show(2.5 == 25 / 10, 3 > 2.9999999999999996, 9007199254740993 == 9007199254740992.0)
show(9007199254740993 > 9007199254740992.0, 9007199254740993.0 < 9007199254740993)
show(2 ** 1023 * 1.0 < 2 ** 1024, 2.0 ** 1023 < 2 ** 1024 - 1, 1.7976931348623157e308 < 2 ** 1024)
show(0.0 == False, 1.0 == True, 1.0 < True)

# hashing: equal values hash equal; fixed values match CPython
show(hash(1.0) if False else 1.0.__hash__(), 2.5.__hash__(), (-1.0).__hash__(), (-2.0).__hash__(), 0.0.__hash__())
show((-0.0).__hash__(), 0.1.__hash__(), 1e300.__hash__(), inf.__hash__(), ninf.__hash__(), 1e-300.__hash__())
show((2.0 ** 61).__hash__(), (2.0 ** 62).__hash__(), (2 ** 61 - 1) * 1.0, 123456.789.__hash__())
show(3.0.__hash__() == 3 .__hash__(), 0.5.__hash__() == (1).__truediv__(2).__hash__())

# conversions
show(3.7.__int__(), (-3.7).__int__(), 3.7.__trunc__(), (-3.7).__floor__(), 3.2.__ceil__(), (-3.2).__ceil__())
show(1e30.__int__(), (-1e30).__int__(), 2.0 ** 70 .__int__() if False else (2.0 ** 70).__int__())
err(inf.__int__)
err(nan.__int__)
err(inf.__floor__)
err(nan.__ceil__)
show(int(2.9), int(-2.9), float(3), float(10 ** 30), float(True), float(2 ** 1023))
err(float, 10 ** 400)
err(float, [])
err(float, None)
err(float, 1, 2)
err(lambda: float(x=1))

# round
show(2.5.__round__(), 3.5.__round__(), (-2.5).__round__(), 0.5.__round__(), 1.5.__round__(), 2.4.__round__(), 2.6.__round__())
show(2.675.__round__(2), 1.005.__round__(2), 0.1234567.__round__(3), 123456.789.__round__(-2), 0.5.__round__(0))
show(1e22.__round__(-5), 5e-324.__round__(300), 1.5.__round__(400), 1.5.__round__(-400), (-1.5).__round__(-400))
show(1.5.__round__(10 ** 30), 1.5.__round__(-10 ** 30), 1.23456789.__round__(None), 12345.6789.__round__(-1))
show(1e300.__round__(-299), 2.5.__round__(0), 25.0.__round__(-1), 35.0.__round__(-1))
show(inf.__round__(2), nan.__round__(2), ninf.__round__(-2), 0.0.__round__(5), (-0.0).__round__(5))
err(inf.__round__)
err(nan.__round__)
err(lambda: 1.5.__round__(1.5))
err(lambda: 1.5.__round__('a'))
err(lambda: 1.7976931348623157e308.__round__(-307) if False else 1.7976931348623157e308.__round__(-308))

# methods
show(2.5.is_integer(), 3.0.is_integer(), inf.is_integer(), nan.is_integer(), (-0.0).is_integer(), 1e300.is_integer())
show(0.5.as_integer_ratio(), 3.0.as_integer_ratio(), (-0.75).as_integer_ratio(), 0.1.as_integer_ratio(), 0.0.as_integer_ratio())
show(1e300.as_integer_ratio(), 5e-324.as_integer_ratio(), 1.7976931348623157e308.as_integer_ratio()[1])
err(inf.as_integer_ratio)
err(nan.as_integer_ratio)
show(1.5.conjugate(), 1.5.real, 1.5.imag, (-0.0).real, 1.5.__float__(), 1.5.__getnewargs__())
show(1.0.hex(), 0.5.hex(), (-0.1).hex(), 3.14159.hex(), 0.0.hex(), (-0.0).hex(), inf.hex(), ninf.hex(), nan.hex(), 5e-324.hex(), 2.2250738585072014e-308.hex(), 1.7976931348623157e308.hex())
show(float.fromhex('0x1.ffffp10'), float.fromhex('-0x1p-1074'), float.fromhex('0x.8'), float.fromhex('  0X1P+3  '), float.fromhex('inf'), float.fromhex('-Infinity'), float.fromhex('nan'))
show(float.fromhex('0x1.fffffffffffff8p0'), float.fromhex('0x1.00000000000008p0'), float.fromhex('0x1.00000000000018p0'), float.fromhex('1p-1080'), float.fromhex('0x1p-1075'), float.fromhex('0x1.8p-1075'))
show(float.fromhex('0x0.0000000000001p-1022'), float.fromhex('ff'), float.fromhex('0'), float.fromhex('-0x0p0'), float.fromhex('0x1p1023'))
err(float.fromhex, '0x1p1024')
err(float.fromhex, '0x1.fffffffffffff8p1023')
err(float.fromhex, '')
err(float.fromhex, 'zz')
err(float.fromhex, '0x1p')
err(float.fromhex, '0x' + '1' * 100000)
err(float.fromhex, 1)
err(float.fromhex, '1.5 x')
show(float.__getformat__('double'), float.__getformat__('float'))
err(float.__getformat__, 'x')
err(float.__getformat__, 1)
show(float.from_number(3), float.from_number(2.5), float.from_number(True))
err(float.from_number, 'x')
err(float.from_number, None)

# parsing
for s in ('1.5', ' 1.5 ', '\t-2e3\n', '+.5', '5.', '1e5', '1E-5', 'inf', '-Inf', 'infinity', '+INFINITY', 'nan', '-NaN',
          '1_0.5', '1_000', '0.000_1', '1e1_0', '1__0', '_1', '1_', '1_.5', '1._5', '', ' ', '1 2', '1.5x', '0x10', 'e5', '.', '-', '--1',
          '١٢٣.٥', '1 ', ' 1.5 ', 'in f', 'infinit', '1e', '1e+', '1e400', '-1e400', '1e-400', '1' * 400, '0.' + '0' * 400 + '1',
          '123456789012345678901234567890', '2.2250738585072011e-308', '4.9406564584124654e-324', '2.4703282292062327e-324', '2.4703282292062328e-324',
          '1.7976931348623158e308', '1.7976931348623159e308', '9007199254740993', '9007199254740993.0000000000000001', '0.1e-0', '1\x00', '\x001'):
    try:
        print(f"{s!r}", f"{float(s)!r}")
    except ValueError as e:
        print(f"{s!r}", e)
err(float, b'1.5')
err(float, b' 2e2 ')
err(float, b'1_5')
class F:
    def __float__(self):
        return 2.5
class I:
    def __index__(self):
        return 7
class Bad:
    def __float__(self):
        return 1
show(float(F()), float(I()))
err(float, Bad())
err(float, object())

# subclass
class MyF(float):
    pass
m = MyF(2.5)
show(m, type(m) is MyF, m + 1, 1 + m, m == 2.5, m.__hash__() == 2.5.__hash__(), m.__float__(), type(m.__float__()) is float, -m, type(-m) is float)
show(MyF('3.5'), MyF(), MyF(7), m.real, m.__round__(), m.__round__(0), m.conjugate(), m.__getnewargs__())
show(MyF.from_number(3), type(MyF.from_number(3)) is MyF, MyF.fromhex('0x1p3'))
show(m < 3, 3 > m, m <= m, 2.5 >= m)

# truth
show(1.0 if 0.5 else 2.0, 1.0 if 0.0 else 2.0, 1.0 if -0.0 else 2.0, 1.0 if nan else 2.0, 1.0 if inf else 2.0, 0.0.__bool__(), 0.1.__bool__())

# format
for v in (1.5, -1.5, 0.0, -0.0, 1234567.891, 1e-7, 1e20, 123456789.0, inf, ninf, nan, 0.000123456, 2.5, 3.5, 1e100, 99.99, 0.05):
    for sp in ('', 'f', '.0f', '.2f', '10.3f', '<10.3f', '^10.3f', '+.1f', ' .1f', '010.2f', '0=+10.2f', '*>12.3e', 'e', '.0e', '#.0e', '#.0f', '#g', 'g', '.3g', '.10g', '#.3g', 'G', 'E', 'F', '.2%', '%', '010%', ',', ',.2f', '_.3f', '_', ',.3e', '.2_f', '.3,f', '.5_g', '15,.2f', '015,.2f', '_>15,.2f', 'n', '.3n', 'z.1f', 'z', '+z.0f', '#', '#g', '#10', '10', '=10', '<5', '.0', '.1', '.20f', '.50e', '#.20g'):
        try:
            print(f"{v!r:>24} {sp:>12} -> {format(v, sp)!r}" if False else f"{v!r} {sp!r} {v.__format__(sp)!r}")
        except ValueError as e:
            print(f"{v!r} {sp!r} ValueError {e}")
err(1.5.__format__, 'd')
err(1.5.__format__, 'x')
err(1.5.__format__, 's')
err(1.5.__format__, 'q')
err(1.5.__format__, '.')
err(1.5.__format__, '_,')
err(1.5.__format__, ',_')
err(1.5.__format__, ',n')
err(1.5.__format__, '.2,_f')
err(1.5.__format__, '.99999999999f')
err(1.5.__format__, 1)
err(1.5.__format__, 'ff')
show(f"{2.5:g}", f"{2.5!r:>8}", f"{1e10:.3}", f"{12345.678:.2e}", "%f" % 1.5, "%.2f" % 2.675, "%e" % 12345.678, "%g" % 1e-5, "%10.3f|" % 3.14159, "%-10.2f|" % 2.5, "%+.1f" % 2.5, "%05.1f" % -2.5, "%#g" % 1.5, "%d" % 3.9, "%i" % -3.9, "%s" % 1.5, "%r" % 0.1, "%x" % 255, "%F" % 1.5, "%G" % 1e-10, "%E" % 1.5)
show(f"{3:f}", f"{3:.2f}", f"{True:.1f}", f"{10 ** 20:e}", f"{3:%}", "%f" % 3, "%e" % 10, "%g" % 100000000, "%.3f" % 10 ** 20)
err(lambda: "%d" % inf)
err(lambda: "%d" % nan)
err(lambda: "%f" % 'a')
err(lambda: f"{10 ** 400:f}")

# int <-> float primitives: rounding of big ints, exact true division
show(float(2 ** 53 + 1), float(2 ** 53 + 3), float(2 ** 54 + 2), float(2 ** 54 + 6), float(-(2 ** 53 + 1)))
show(float(2 ** 1024 - 2 ** 970 - 1))
err(float, 2 ** 1024 - 2 ** 970)
show((2 ** 53 + 1) / 1, 1 / 3, (2 ** 80 + 1) / 2 ** 80, 10 ** 30 / 10 ** 10, 7 / 10 ** 400, 10 ** 400 / 3 ** 600, 1 / 10 ** 323, 1 / 10 ** 324, -1 / 10 ** 400)
show(int(1e22), int(2.0 ** 52 + 0.5), int(-0.9), int(1.7976931348623157e308) % 1000, int(5e-324))
err(lambda: 10 ** 400 / 10 ** -1 if False else 10 ** 1000 / 3)
err(lambda: 1 / 0)
