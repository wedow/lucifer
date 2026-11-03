# str methods over ASCII, Latin-1, UCS-2 and UCS-4 data. Lists are shown joined,
# as the list type arrives later.
def bs(b):
    return repr(str(b, 'latin-1'))


def j(parts):
    return '<' + '|'.join(parts) + '>'

ascii_s = 'Hello, World'
latin = 'caf\xe9 \xfcber'
bmp = 'naïve Δελτα €'
astral = 'a\U0001f600b\U00010348c'
for s in (ascii_s, latin, bmp, astral, ''):
    print(repr(s), s.__len__(), s.upper(), s.lower(), s.swapcase(), s.title(), s.capitalize(), s.casefold())
    print(s.isascii(), s.isalpha(), s.isalnum(), s.isdigit(), s.isdecimal(), s.isnumeric(), s.isspace(),
          s.islower(), s.isupper(), s.istitle(), s.isprintable(), s.isidentifier())
    print(ascii(s), str(s.encode('utf-8'), 'latin-1').__len__(), s * 3 == s + s + s, s.__hash__() == s.__hash__())

# predicates
for s in ('abc', 'ABC', 'Abc', 'aBc', 'a1', '123', '٣٤', '²', '½', ' \t\n', ' ', 'x y',
          '_a1', '1a', 'class', 'été', 'ǅ', 'İ', '\xed\xa0\x80'):
    print(repr(s), s.isalpha(), s.isalnum(), s.isdigit(), s.isdecimal(), s.isnumeric(), s.isspace(),
          s.islower(), s.isupper(), s.istitle(), s.isprintable(), s.isidentifier())

# case mapping with special cases
for s in ('stra\xdfe', 'ΣΑΣ', 'aΣ', 'Σ', 'ABCΣ DEF', 'ﬁ', 'ŉ', 'ǆ', 'ǆ',
          'o\'neil mcDONALD 3rd', 'hello world-foo_bar', 'ẞ', 'i̇', 'İ', 'K'):
    print(repr(s), repr(s.upper()), repr(s.lower()), repr(s.title()), repr(s.capitalize()),
          repr(s.swapcase()), repr(s.casefold()))

# find family
t = 'abcabcabc'
print(t.find('abc'), t.find('abc', 1), t.find('abc', 1, 5), t.find('abc', -3), t.find(''), t.find('', 9), t.find('', 10),
      t.find('x'), t.find('c', -100, 100), t.find('abc', 7))
print(t.rfind('abc'), t.rfind('abc', 0, 8), t.rfind('abc', 4), t.rfind(''), t.rfind('', 2, 5), t.rfind('x'))
print(t.index('bc'), t.rindex('bc'), t.count('abc'), t.count('a'), t.count(''), t.count('', 2, 5), t.count('abc', 1),
      t.count('aa'), 'aaaa'.count('aa'))
print('caf\xe9'.find('\xe9'), 'caf\xe9'.find('e'), bmp.find('ε'), astral.find('\U00010348'), astral.rfind('c'))
print(t.startswith('abc'), t.startswith('bc', 1), t.startswith(('x', 'ab')), t.startswith('abc', 7), t.startswith('', 9),
      t.startswith('', 10), t.endswith('abc'), t.endswith('ab', 0, 8), t.endswith(('x', 'bc')), t.endswith('', 3, 2))
print('a' in t, 'ca' in t, 'x' in t, '' in t, 'ab' not in t, '€' in bmp)

# slicing through __getitem__ and iteration
print(t.__getitem__(0), t.__getitem__(-1), bmp.__getitem__(8), astral.__getitem__(1))
print(j([c for c in astral]))

# split and friends
print(j('a b  c   d'.split()), j(' a b '.split()), j(''.split()), j('   '.split()), j('a b c d'.split(None, 2)))
print(j('a,b,,c'.split(',')), j(',a,'.split(',')), j(''.split(',')), j('a,b,c'.split(',', 1)), j('abcabc'.split('bc')))
print(j('a  b c '.rsplit()), j('a b c d'.rsplit(None, 2)), j('a,b,c'.rsplit(',', 1)), j('a,b,c'.rsplit(',')), j('abcabc'.rsplit('bc', 1)))
print(j('a\tb\n c\x0bd\x0ce f\xa0g'.split()), j('\x1cx\x1dy\x1ez\x1f'.split()))
print(j('line1\nline2\r\nline3\rline4\x0bx\x0cy\x1cz\x1d\x1e\x85  e'.splitlines()),
      j('a\nb\r\n\nc\n'.splitlines(True)), j(''.splitlines()), j('\n'.splitlines()), j('a\r\nb'.splitlines(keepends=True)))
print(j('abcdef'.partition('cd')), j('abcdef'.partition('x')), j('abcdef'.rpartition('c')), j('abcabc'.rpartition('b')),
      j('abc'.rpartition('x')), j('abc'.partition('c')))
print(','.join(['a', 'b', 'c']), ','.join(('a',)), ','.join([]), ''.join(['x', 'y']), '€'.join(['a', 'b', 'c']), ' '.join([c for c in 'xyz']))

# replace
print('aaa'.replace('a', 'b'), 'aaa'.replace('a', 'b', 2), 'aaa'.replace('a', 'b', 0), 'aaa'.replace('a', 'b', -1),
      'abc'.replace('', '-'), 'abc'.replace('', '-', 2), ''.replace('', 'x'), ''.replace('a', 'x'),
      'abab'.replace('ab', ''), 'abc'.replace('b', 'long replacement'), 'caf\xe9'.replace('\xe9', '€'),
      'x€x'.replace('€', 'e'), 'aXbXc'.replace('X', '\U0001f600'))

# strip
print(repr('  x  '.strip()), repr('  x  '.lstrip()), repr('  x  '.rstrip()), repr('xxhixx'.strip('x')),
      repr('xxhixx'.lstrip('x')), repr('xxhixx'.rstrip('x')), repr('abcba'.strip('ab')), repr(''.strip()), repr('   '.strip()),
      repr('\t\n x \xa0'.strip()), repr('x'.strip('')), repr('xyx'.strip(None)))
print('prefix-body'.removeprefix('prefix-'), 'prefix-body'.removeprefix('x'), 'body.suffix'.removesuffix('.suffix'),
      'body'.removesuffix(''), 'aaa'.removeprefix('aaa'))

# padding
print(repr('ab'.center(7)), repr('ab'.center(7, '*')), repr('abc'.center(6, '*')), repr('ab'.center(1)), repr('ab'.ljust(5, '.')),
      repr('ab'.rjust(5, '.')), repr('ab'.ljust(1)), repr('42'.zfill(5)), repr('-42'.zfill(5)), repr('+42'.zfill(5)),
      repr('abc'.zfill(2)), repr(''.zfill(3)), repr('€'.rjust(3, 'é')))
print(repr('a\tb\tc'.expandtabs()), repr('a\tb\tc'.expandtabs(4)), repr('a\tb\nc\td'.expandtabs(3)), repr('a\tb'.expandtabs(0)),
      repr('\t'.expandtabs(1)), repr('ab\r\tc'.expandtabs(4)), repr('€\tx'.expandtabs(4)))

# encode and decode
for s in (ascii_s, latin, bmp, astral):
    e = s.encode()
    print(bs(e), bs(s.encode('utf-8')) == bs(e), bs(s.encode('UTF8')) == bs(e), bs(s.encode('utf_8')) == bs(e))
    print(repr(str(e, 'utf-8')))
print(bs('caf\xe9'.encode('latin-1')), bs('caf\xe9'.encode('iso-8859-1')), bs('abc'.encode('ascii')), bs('abc'.encode('us-ascii')))
print(bs('caf\xe9€\ud800'.encode('ascii', 'ignore')), bs('caf\xe9€'.encode('ascii', 'replace')),
      bs('caf\xe9€'.encode('ascii', 'backslashreplace')), bs('caf\xe9€'.encode('ascii', 'xmlcharrefreplace')),
      bs('caf\xe9€'.encode('latin-1', 'replace')), bs('\udc80\udcff'.encode('utf-8', 'surrogateescape')),
      bs('a\ud800b'.encode('utf-8', 'surrogatepass')), bs('a\ud800b'.encode('utf-8', 'ignore')),
      bs('a\ud800b'.encode('utf-8', 'replace')))
print(repr(str(b'caf\xc3\xa9', 'utf-8')), repr(str(b'caf\xe9', 'latin-1')), repr(str(b'abc', 'ascii')),
      repr(str(b'a\xffb', 'utf-8', 'replace')), repr(str(b'a\xffb', 'utf-8', 'ignore')),
      repr(str(b'a\xffb', 'utf-8', 'backslashreplace')), repr(str(b'a\xffb', 'utf-8', 'surrogateescape')),
      repr(str(b'a\xed\xa0\x80b', 'utf-8', 'surrogatepass')), repr(str(b'\xe2\x82', 'utf-8', 'replace')),
      repr(str(b'\xf0\x9f\x98', 'utf-8', 'replace')), repr(str(b'\xc3(', 'utf-8', 'replace')),
      repr(str(b'\xe2(\xa1', 'utf-8', 'replace')), repr(str(b'\xed\xa0\x80', 'utf-8', 'replace')),
      repr(str(b'\xf4\x90\x80\x80', 'utf-8', 'replace')), repr(str(b'\xc0\x80', 'utf-8', 'replace')))

# comparison, concatenation, repetition
print('a' < 'b', 'a' < 'a', 'a' <= 'a', 'b' > 'a', 'a' >= 'b', 'a' == 'a', 'a' != 'a', 'ab' < 'abc', '' < 'a',
      'z' < '\xe9', '\xe9' < '€', '€' < '\U0001f600', 'a€' < 'a\U0001f600', 'abc' == 'abc', 'abc' == 'abd',
      '\xe9' == 'é', 'a'.__eq__(1) is NotImplemented, 'a'.__lt__(1) is NotImplemented, 'a'.__ne__('b'))
print('a' + 'b', 'a' + '', '' + 'b', 'caf\xe9' + '€', 'a' * 3, 3 * 'ab', 'a' * 0, 'a' * -1, '' * 5, 'ab'.__mul__(2), 'ab'.__rmul__(2),
      'a'.__add__('b'), 'a'.__mod__(()) )
x = 'a'
x += 'b'
x *= 3
print(x)

# translate with a table of __getitem__
class Table:
    def __getitem__(self, i):
        if i == 97:
            return 'AA'
        if i == 98:
            return None
        if i == 99:
            return 120
        raise LookupError(i)
print('abcd'.translate(Table()), '€abc'.translate(Table()))

# hash is stable per value
print('abc'.__hash__() == 'abc'.__hash__(), 'abc'.__hash__() == 'abd'.__hash__(), ''.__hash__(), 'a'.__hash__() == ('a' + '').__hash__())

# subclass
class S(str):
    pass
s = S('caf\xe9')
print(repr(s), s.upper(), type(s.upper()).__name__, s == 'caf\xe9', s.__len__(), s + 'x', 'x' + s, s.__hash__() == 'caf\xe9'.__hash__(),
      type(s).__name__, isinstance(s, str), repr(S('')), S('abc').find('c'), j(S('a b').split()))
class S2(str):
    def __new__(cls, v):
        return str.__new__(cls, v + '!')
print(S2('hi'), repr(S2('hi')), str(S2('hi')) == 'hi!', type(str(S2('hi'))).__name__)

# str()
print(str(), str(1), str('x'), str(True), str(None), str(S('q')), type(str(S('q'))).__name__, str(b'abc', 'ascii'))
class Strable:
    def __str__(self):
        return 'strable'
    def __repr__(self):
        return 'REPR'
print(str(Strable()), repr(Strable()), ascii(Strable()), '%s %r' % (Strable(), Strable()), f'{Strable()!r}')
print(repr("it's"), repr('say "hi"'), repr('both \' and "'), repr('back\\slash'), repr('tab\there'), repr('nl\n\r'),
      repr('\x00\x07\x1b\x7f\x80\x9f\xa0\xad'), repr('͸​ ﻿￾\U000e0001\U0010ffff'),
      repr('é中́\U0001f600'), ascii('é中\U0001f600"\''))
