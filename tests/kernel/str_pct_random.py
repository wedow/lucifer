# Random %-conversions over ints and strs: the results, or the error raised, must match.
seed = 99


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return (seed >> 8) % n


def pick(n):
    for x in (0, 1, -1, 255, -255, 123456789, -2 ** 70, True, 65, 0x10ffff, 'a', 'hello', 'caf\xe9', '', '€', None, 1000):
        if n == 0:
            return x
        n -= 1


FLAGS = '-+ #0'
TYPES = 'diouxXcsrasyc%'

i = 0
while i < 2500:
    fmt = '%'
    n = rnd(3)
    while n > 0:
        fmt += FLAGS.__getitem__(rnd(5))
        n -= 1
    if rnd(2):
        fmt += '%d' % rnd(14)
    if rnd(3) == 0:
        fmt += '.%d' % rnd(6)
    fmt += TYPES.__getitem__(rnd(14))
    v = pick(rnd(17))
    try:
        print(repr(fmt), repr(v), repr(fmt % (v,)), repr(('[' + fmt + ']') % v))
    except Exception as e:
        print(repr(fmt), repr(v), type(e).__name__, e)
    i += 1
