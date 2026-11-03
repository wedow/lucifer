# Random strings over a pool that mixes every storage width, case-mapping specials,
# whitespace and combining marks, through the methods that scan and rebuild text.
seed = 20240229


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return (seed >> 8) % n


POOL = 'aabbAB  \t\n.,\xe9\xdfΣςσİǆǅ€中\U0001f600́\xa0 ٣\xb2_xZﬁẞ\xb5ͅ'
N = POOL.__len__()


def word(limit):
    out = ''
    n = rnd(limit + 1)
    while n > 0:
        out += POOL.__getitem__(rnd(N))
        n -= 1
    return out


def j(parts):
    return '<' + '|'.join(parts) + '>'


i = 0
while i < 400:
    s = word(12)
    t = word(3)
    u = word(3)
    a = rnd(14) - 3
    b = rnd(14) - 3
    print(repr(s), repr(t), repr(u), a, b)
    print(s.upper(), s.lower(), s.swapcase(), s.title(), s.capitalize(), s.casefold())
    print(s.isalpha(), s.isalnum(), s.isdecimal(), s.isdigit(), s.isnumeric(), s.isspace(), s.islower(),
          s.isupper(), s.istitle(), s.isprintable(), s.isidentifier(), s.isascii())
    print(s.find(t), s.find(t, a), s.find(t, a, b), s.rfind(t), s.rfind(t, a), s.rfind(t, a, b),
          s.count(t), s.count(t, a), s.count(t, a, b), s.startswith(t, a), s.endswith(t, a, b), t in s)
    print(repr(s.replace(t, u)), repr(s.replace(t, u, 1)), repr(s.replace(t, u, a)))
    if t:
        print(j(s.split(t)), j(s.rsplit(t)), j(s.split(t, 1)), j(s.rsplit(t, 2)),
              j(s.partition(t)), j(s.rpartition(t)))
    print(j(s.split()), j(s.rsplit()), j(s.split(None, 2)), j(s.rsplit(None, 1)), j(s.splitlines()), j(s.splitlines(True)))
    print(repr(s.strip()), repr(s.lstrip()), repr(s.rstrip()), repr(s.strip(t)), repr(s.lstrip(t)), repr(s.rstrip(t)))
    print(repr(s.center(a + 5)), repr(s.ljust(a + 5, 'x')), repr(s.rjust(a + 5, '€')), repr(s.zfill(a + 5)),
          repr(s.expandtabs(a + 3)))
    print(s < t, s <= t, s == t, s != t, s > t, s >= t, repr(s + t), repr(s * (a & 3)), repr(t.join([s, u, t])))
    print(repr(str(s.encode('utf-8', 'replace'), 'latin-1')), repr(str(s.encode('utf-8', 'replace'), 'utf-8')),
          repr(str(s.encode('latin-1', 'replace'), 'latin-1')))
    print(repr(str(s.encode('ascii', 'backslashreplace'), 'ascii')), repr(str(s.encode('ascii', 'xmlcharrefreplace'), 'ascii')),
          ascii(s), repr(s.removeprefix(t)), repr(s.removesuffix(t)), s.__hash__() == (s + '').__hash__())
    i += 1
