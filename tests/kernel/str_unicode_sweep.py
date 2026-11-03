# Every 7th code point through repr, the predicates and the case maps, folded into digests.
digest = 0
c = 0
while c < 0x110000:
    s = '%c' % c
    r = repr(s)
    bits = (s.isprintable() + 2 * s.isalpha() + 4 * s.isupper() + 8 * s.islower() + 16 * s.isdigit()
            + 32 * s.isspace() + 64 * s.isidentifier() + 128 * s.istitle() + 256 * s.isnumeric() + 512 * s.isdecimal())
    u = s.upper()
    l = s.lower()
    t = s.title()
    f = s.casefold()
    digest = (digest * 31 + bits + 1024 * r.__len__() + 65536 * (u == s) + 131072 * (l == s)
              + 262144 * (t == s) + 524288 * (f == s)) % 2305843009213693951
    digest = (digest * 31 + u.__len__() + 4 * l.__len__() + 16 * f.__len__() + 64 * t.__len__()) % 2305843009213693951
    if c % 0x10000 < 7:
        print(c, r, u, l, t, f, bits, digest)
    c += 7
print(digest)
