# complex arithmetic, comparison, repr, construction, parsing, hashing and formatting.
inf = 1e308 * 10
nan = inf - inf

def show(*xs):
    print(*[f"{x!r}" for x in xs])

def err(f, *a):
    try:
        print(f(*a))
    except BaseException as e:
        print(type(e).__name__, e)

z = 3 + 4j
w = 1 - 2j
show(z, w, 1j, -1j, 0j, -0j, complex(0.0, -0.0), complex(-0.0, 0.0), 1.5j, 1e20j, 1e-5 + 1e-5j, complex(inf, nan), complex(nan, -inf), 2 + 0j)
show(z + w, z - w, z * w, z / w, w / z, z + 1, 1 + z, z - 1.5, 1.5 - z, z * 2, 2 * z, z / 2, 2 / z, z * 0.5, 0.5 / z)
show(z ** 2, z ** -2, z ** 0, z ** 1, z ** 0.5, z ** w, 2 ** z, 2.0 ** z, z ** 2.0, z ** 100, z ** 101, (1j) ** 2, 0j ** 0, 0j ** 2, z ** 3j)
show(-z, +z, z.conjugate(), z.real, z.imag, z.__neg__(), z.__pos__(), z.__complex__(), z.__getnewargs__())
show(z.__abs__(), (3 + 4j).__abs__(), 1e200j.__abs__(), complex(inf, nan).__abs__(), complex(nan, inf).__abs__(), complex(nan, 1).__abs__())
show(z == z, z == w, z != w, z == 3 + 4j, 1 + 0j == 1, 1 + 0j == 1.0, 1j == 1, 2.5 + 0j == 2.5, 2 ** 100 + 0j == 2 ** 100, 2 ** 100 + 0j == 2 ** 100 + 1, 1 == 1 + 0j, 1.0 == 1 + 0j, 1e300 + 0j == 10 ** 300)
show(z.__eq__(1), z.__eq__('a'), z.__lt__(1), z.__ne__(z), nan + 0j == nan, complex(nan, 0) != complex(nan, 0))
show(z.__hash__(), w.__hash__(), (1 + 0j).__hash__() == 1.0.__hash__(), (2.5 + 0j).__hash__() == 2.5.__hash__(), 1j.__hash__(), complex(inf, 0).__hash__(), complex(0, inf).__hash__(), complex(1.5, -2.5).__hash__(), (1e300 + 1e-300j).__hash__())
show(z.__bool__(), 0j.__bool__(), 1j.__bool__(), complex(0.0, -0.0).__bool__(), complex(nan, 0).__bool__())

# mixed real operands are not first coerced to complex
show(complex(inf, 1) * 2, 2 * complex(inf, 1), complex(inf, 1) * 2.0, inf * (1 + 2j), complex(nan, 1) + 1, complex(1, 2) / inf, complex(inf, inf) / 2, 1 / complex(0, inf))
show(complex(1, nan) * complex(inf, 1), complex(inf, nan) * 1j, complex(1e308, 1e308) * complex(1e308, 1e308), complex(1e308, 1e308) / complex(1e-308, 1e-308))
show(complex(inf, 0) / complex(inf, 0), complex(1, 2) / complex(inf, 0), complex(1, 2) / complex(0, inf), complex(inf, 1) / complex(1, 2), (-0.0) + 0j, 0j - 0.0, -0.0 - 0j, 0j * -1)

# construction
show(complex(), complex(1), complex(1.5), complex(1, 2), complex(1.5, -2.5), complex(True, False), complex(z), complex(z))
show(complex(real=1), complex(imag=2), complex(real=1, imag=2), complex(1, imag=2), complex(10 ** 20, 2 ** 70))
show(complex('1+2j'), complex(' 1.5-2.5j '), complex('1j'), complex('-j'), complex('j'), complex('+j'), complex('1+j'), complex('1-j'), complex('(1+2j)'), complex('( 1+2j )'), complex('3'), complex('-3.5'), complex('inf'), complex('nan'), complex('infj'), complex('1e3+1e3j'), complex('1_0+2_0j'), complex('1J'), complex('١+٢j'))
show(complex('nan+nanj'), complex('-inf-infj'), complex('1+infj'), complex('0x'.replace('x', '')), complex('1e400+1e-400j'))
for s in ('', ' ', '1+', '1++2j', '1+2', 'j1', '1 + 2j', '1+2jj', '(1+2j', '1+2j)', '()', '(', 'abc', '1_+2j', '1+_2j', '1__0j', '1+2 j', '1\x00', '1+2j\x00', 'infinity+1j', 'i', '--1', '1+-2j', '+-1j'):
    try:
        print(f"{s!r}", f"{complex(s)!r}")
    except ValueError as e:
        print(f"{s!r}", e)
err(complex, [])
err(complex, None)
err(complex, 'a', 1)
err(complex, 1, 'a')
err(complex, 1, 2, 3)
err(lambda: complex(1, 2, imag=3))
err(lambda: complex(1, real=3))
err(lambda: complex(foo=1))
err(complex, object())
err(complex, 10 ** 400)
err(complex, 1, 10 ** 400)
class C:
    def __complex__(self):
        return 1 + 2j
class R:
    def __float__(self):
        return 2.5
class I:
    def __index__(self):
        return 3
class Bad:
    def __complex__(self):
        return 1
show(complex(C()), complex(R()), complex(I()), complex(1, R()), complex(R(), I()))
err(complex, Bad())

# errors
err(lambda: z / 0)
err(lambda: z / 0j)
err(lambda: z / 0.0)
err(lambda: 1 / 0j)
err(lambda: 1.5 / 0j)
err(lambda: 0j ** -1)
err(lambda: 0j ** 1j)
err(lambda: 0.0 ** 1j)
err(lambda: z ** 10000)
err(lambda: 1e300j ** 2)
err(lambda: complex(1e200, 1e200).__abs__() * 1e200)
err(lambda: z // 2)
err(lambda: z % 2)
err(lambda: 2 // z)
err(lambda: z < 1)
err(lambda: z <= z)
err(lambda: 1 > z)
err(lambda: z + 'a')
err(lambda: 'a' * z)
err(lambda: z.__pow__(2, 3))
err(lambda: z + 10 ** 400)
err(lambda: 10 ** 400 + z)
err(lambda: divmod(z, 2) if False else z.__divmod__(2))

# subclass
class MyC(complex):
    pass
m = MyC(1, 2)
show(m, type(m) is MyC, m + 1, 1 + m, m == 1 + 2j, m.__hash__() == (1 + 2j).__hash__(), m.__complex__(), type(m.__complex__()) is complex, type(-m) is complex, type(+m) is complex)
show(MyC('1+2j'), MyC(), MyC(3), MyC(m), complex(m), MyC.from_number(3), type(MyC.from_number(3)) is MyC, complex.from_number(2.5), complex.from_number(z) is z, complex.from_number(C()))
err(complex.from_number, 'x')

# format
for v in (3 + 4j, 1.5 - 2.5j, 0j, -0j, complex(-0.0, 0.0), 1j, -1j, complex(inf, nan), complex(nan, -inf), 1e20 + 1e-20j, complex(1234567.891, 0.000123), complex(2.5, 3.5)):
    for sp in ('', 'f', '.2f', '10.2f', '<10.2f', '^20.1f', '>20', '+.1f', ' .1f', '*^25.2e', 'e', '.0e', '#.0f', 'g', '.3g', '#g', 'G', 'E', 'F', ',.2f', '_.3f', ',', '.2_f', 'n', '.3n', 'z.1f', 'z', '.50f', '.0f', '+', '-', ' ', '20', '<20', '^20', '#', '.1', '25.3e', '+z.0f'):
        try:
            print(f"{v!r} {sp!r} {v.__format__(sp)!r}")
        except ValueError as e:
            print(f"{v!r} {sp!r} ValueError {e}")
err(z.__format__, '010')
err(z.__format__, '0=10')
err(z.__format__, '0<10')
err(z.__format__, '=10')
err(z.__format__, '%')
err(z.__format__, 'd')
err(z.__format__, 's')
err(z.__format__, 1)
err(z.__format__, '.99999999999f')
show(f"{z}", f"{z:.1f}", f"{z!r}", f"{z:>20}|", "%s" % z, "%r" % z)
