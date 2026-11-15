# The short paths of the string primitives give what the general ones give:
# integers formatted straight into their str, ASCII case mapping, and
# single-character searches, at every boundary.

# int -> str in bases 10, 2, 8 and 16: one digit, the digit-count edges,
# negatives, and sizes past any small scratch.
values = [0, 1, 7, 9, 10, 99, 100, 999999999, 1000000000, 2**30 - 1, 2**30,
          2**60 - 1, 2**60, 2**63, 10**18, 10**19 - 1, 10**40, 2**300, 7**400, 10**200 - 1]
for v in values:
    for x in (v, -v):
        print(str(x), repr(x), hex(x), oct(x), bin(x)[:80], format(x, 'x')[-20:], len(str(x)))
print(str(2**3000)[:30], len(str(2**3000)), len(str(-(10**600))))
print([str(i) for i in range(-12, 13)])
print(str(5) == '5', str(12) + str(-3), f'{123}{-45}', '%d|%x' % (678, 255))
print(int(str(2**200)) == 2**200, int(hex(2**200), 16) == 2**200)

# ASCII case mapping at the letter edges; one character and empty strings.
edge = '@AZ[`az{ 09~\t'
for s in (edge, edge * 3, 'MiXeD-Case_42', 'a', 'Z', '', 'ab', 'ÀÉ', 'aÀ', 'ß', 'ﬁx'):
    print(repr(s.lower()), repr(s.upper()), repr(s.casefold()), repr(s.swapcase()), repr(s.title()))
print('UTF-8'.lower(), 'Latin-1'.upper(), b'x'.decode('UTF-8'), 'x'.encode('Utf_8'))

# One-character needles: present at the start, the end, the middle, absent,
# repeated, inside slices, in wider kinds, and empty haystacks.
hay = ['', 'a', 'ab', 'ba', 'abc', 'aXbXc', 'xxxx', 'é', 'aé', 'é<', '<é>', '\U0001f600<', 'a' * 100 + '<']
for h in hay:
    for n in ('a', '<', 'é', 'X', '\U0001f600'):
        print(repr(h)[:20], repr(n), h.find(n), h.rfind(n), n in h, h.count(n), h.index(n) if n in h else -1,
              h.find(n, 1), h.find(n, 1, 3), h.find(n, -2), repr(h.replace(n, '&'))[:30],
              repr(h.replace(n, '&', 1))[:30], repr(h.replace(n, '', 0))[:30], h.split(n)[:3], h.partition(n))
s = '<td>&"\'</td>'
for old, new in (('&', '&amp;'), ('<', '&lt;'), ('>', '&gt;'), ('"', '&#34;'), ("'", '&#39;')):
    s = s.replace(old, new)
print(s, 'abc'.replace('b', 'b') == 'abc', 'abc'.find('c', 2**70), 'abc'.find('a', -2**70))

# Replacements whose results are written straight into a new str: every
# count, growing, shrinking to one character or none, and wider kinds,
# which must give the same canonical str as building it by hand.
cases = [('a-b-c', '-', '_'), ('aaaa', 'aa', 'b'), ('abcabc', 'bc', ''), ('xx', 'x', ''), ('ab', 'a', ''),
         ('abc', 'abc', 'Z'), ('aXbX', 'X', 'éé'), ('ééa', 'a', 'b'), ('utf-8', '-', '_'), ('a.b.c.d', '.', '::'),
         ('a', 'a', 'bc'), ('<<>>', '<', '&lt;'), ('ab', 'é', 'x'), ('abab', 'ab', 'ab')]
for s, old, new in cases:
    for cnt in (-1, 0, 1, 2, 10):
        r = s.replace(old, new, cnt)
        built = ''.join(list(r))
        print(repr(r), len(r), r.isascii(), r == built, hash(r) == hash(built), r.encode('utf-8'))
