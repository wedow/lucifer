# Seeded random floats: repr round trips, decimal parsing, formatting, hashing,
# and exact comparison with ints. Outputs must match CPython digit for digit.
seed = 20260314


def rnd(n):
    global seed
    seed = (seed * 6364136223846793005 + 1442695040888963407) % 18446744073709551616
    return (seed >> 33) % n


def rbits(k):
    v = 0
    while k > 0:
        v = v * 2147483648 + rnd(2147483648)
        k -= 31
    return v


def rfloat():
    kind = rnd(6)
    if kind == 0:
        return float(rnd(2000) - 1000)
    if kind == 1:
        return (rnd(2000000) - 1000000) / 1000.0
    m = rbits(53) % 9007199254740992
    e = rnd(2040) - 1074 if kind != 2 else rnd(80) - 40
    x = m * 2.0 ** e if e >= 0 else m / 2 ** -e
    return -x if rnd(2) else x


def rdigits():
    s = ''
    for i in range(1 + rnd(25)):
        s += '0123456789'[rnd(10)]
    return s


SPECS = ('', 'f', 'e', 'g', 'E', 'G', 'F', '%', 'n')
for i in range(1500):
    x = rfloat()
    r = repr(x)
    print(r, float(r) == x, x.__hash__(), x.hex())
    t = SPECS[rnd(9)]
    if rnd(3):
        t = '.' + str(rnd(22)) + t
    if rnd(4) == 0:
        t = ['+', ' ', '#', 'z', ','][rnd(5)] + t
    if rnd(4) == 0:
        t = str(rnd(30)) + t
    if rnd(8) == 0:
        t = ['<', '>', '^', '=', '0', '*^'][rnd(6)] + t
    try:
        print(i, t, x.__format__(t))
    except ValueError as e:
        print(i, t, e)

for i in range(1500):
    s = rdigits()
    form = rnd(4)
    if form == 1:
        s = s + '.' + rdigits()
    elif form == 2:
        s = s[:1] + '.' + s[1:]
    s += 'e' + str(rnd(700) - 350)
    if rnd(4) == 0:
        s = '-' + s
    x = float(s)
    print(s, repr(x), float(repr(x)) == x)

for i in range(800):
    x = rfloat()
    n = rbits(1 + rnd(120)) * (1 if rnd(2) else -1)
    if rnd(3) == 0:
        n = int(x) + rnd(3) - 1
    print(x < n, x <= n, x == n, x != n, x > n, x >= n, n < x, n == x)
    if rnd(2):
        print(x.as_integer_ratio())
    p = rnd(40) - 20
    print(x.__round__(p), x.__floor__(), x.__ceil__(), x.__int__())

for i in range(800):
    a = rfloat()
    b = rfloat()
    if rnd(2):
        b = float(rnd(40) - 20) if rnd(2) else b / 1e300
    for f in ('__add__', '__sub__', '__mul__', '__truediv__', '__floordiv__', '__mod__', '__divmod__'):
        try:
            print(a.__getattribute__(f)(b))
        except ZeroDivisionError as e:
            print(e)
    try:
        print(a ** float(rnd(9) - 4))
    except (OverflowError, ZeroDivisionError) as e:
        print(e)
    print(int(a) / int(b) if int(b) != 0 else 0, 10 ** rnd(25) / (1 + rbits(60)))
