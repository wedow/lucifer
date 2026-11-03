# Random format specs over ints and strs: the results, or the error raised, must match.
seed = 7


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return (seed >> 8) % n


FILLS = '*x0 €<'
ALIGNS = '<>^='
SIGNS = '+- '
TYPES = 'bcdoxXnsr_ydS'
INTS = (0, 1, -1, 255, -255, 123456789, -2 ** 70, True, 65, 0x10ffff, 0x110000, 1000, -1000000, 7)
STRS = ('', 'a', 'hello', 'caf\xe9', '€ uro', 'x' * 20)


def spec():
    out = ''
    k = rnd(4)
    if k == 0:
        out += FILLS.__getitem__(rnd(6)) + ALIGNS.__getitem__(rnd(4))
    elif k == 1:
        out += ALIGNS.__getitem__(rnd(4))
    if rnd(3) == 0:
        out += SIGNS.__getitem__(rnd(3))
    if rnd(8) == 0:
        out += 'z'
    if rnd(5) == 0:
        out += '#'
    if rnd(4) == 0:
        out += '0'
    if rnd(2) == 0:
        out += '%d' % rnd(14)
    if rnd(4) == 0:
        out += ',' if rnd(2) else '_'
    if rnd(6) == 0:
        out += ','
    if rnd(9) == 0:
        out += '.%d' % rnd(6)
    if rnd(2) == 0:
        out += TYPES.__getitem__(rnd(13))
    return out


i = 0
while i < 1500:
    sp = spec()
    v = pick_int(rnd(14)) if rnd(3) else pick_str(rnd(6))
    try:
        print(repr(sp), repr(v), repr(format(v, sp)))
    except Exception as e:
        print(repr(sp), repr(v), type(e).__name__, e)
    try:
        print(repr('{:%s}' % sp), repr('{:{}}'.format(v, sp)), repr(f'{v:{sp}}'))
    except Exception as e:
        print(type(e).__name__, e)
    i += 1
