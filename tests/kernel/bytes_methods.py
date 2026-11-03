# bytes and bytearray methods, run on both types; results are shown as reprs.
def r(x):
    return repr(x)


for T in (bytes, bytearray):
    s = T(b'Hello, World  abcabc')
    print(T.__name__, r(s), len(s), s[0], s[-1], r(s[1:4]), r(s[::-2]), r(s[::3]), r(s[5:2]), r(s[100:]))
    print(s.find(b'abc'), s.find(b'abc', 14), s.find(b'abc', -3), s.find(b'', 20), s.find(b'', 21), s.find(b'o', 5, 8),
          s.find(97), s.rfind(b'abc'), s.rfind(b'abc', 0, 19), s.rfind(b''), s.rfind(b'', 3, 5), s.find(b'zz'))
    print(s.index(b'W'), s.rindex(b'c'), s.count(b'abc'), s.count(b'a'), s.count(b''), s.count(b'', 5), s.count(b'', 25),
          s.count(b'a', 1, 14), s.count(97), s.count(b'aa'), T(b'aaaa').count(b'aa'))
    print(s.startswith(b'Hello'), s.startswith(b'ello', 1), s.startswith((b'x', b'He')), s.startswith(b'', 20), s.startswith(b'', 21),
          s.endswith(b'abc'), s.endswith(b'ab', 0, 19), s.endswith((b'x', b'bc')), s.endswith(b'', 3, 2))
    print(100 in s, 300 in T(b'') if False else False, b'World' in s, b'' in s, b'xyz' in s)
    print(r(s.upper()), r(s.lower()), r(s.swapcase()), r(s.title()), r(s.capitalize()))
    print(s.isalpha(), s.isalnum(), s.isdigit(), s.isspace(), s.islower(), s.isupper(), s.istitle(), s.isascii())
    for x in (b'', b'abc', b'ABC', b'Abc', b'a1', b'123', b' \t\n\r\x0b\x0c', b'Ab Cd', b'AB cd', b'\xe9', b'a b'):
        t = T(x)
        print(r(x), t.isalpha(), t.isalnum(), t.isdigit(), t.isspace(), t.islower(), t.isupper(), t.istitle(), t.isascii(),
              r(t.title()), r(t.swapcase()), r(t.capitalize()))
    print([r(p) for p in s.split()], [r(p) for p in s.split(b'l')], [r(p) for p in s.split(b'l', 1)],
          [r(p) for p in s.split(None, 2)], [r(p) for p in T(b'  a  b  ').split()], [r(p) for p in T(b'').split()],
          [r(p) for p in T(b'a,b,,c').split(b',')], [r(p) for p in T(b'abcabc').split(b'bc')], [r(p) for p in T(b' a b c ').split(maxsplit=1)])
    print([r(p) for p in s.rsplit()], [r(p) for p in s.rsplit(b'l')], [r(p) for p in s.rsplit(b'l', 1)],
          [r(p) for p in T(b'a b c d').rsplit(None, 2)], [r(p) for p in T(b'  a  b  ').rsplit(None, 1)], [r(p) for p in T(b'abcabc').rsplit(b'bc', 1)])
    print([r(p) for p in T(b'a\nb\r\nc\rd\n').splitlines()], [r(p) for p in T(b'a\nb\r\nc\rd\n').splitlines(True)],
          [r(p) for p in T(b'').splitlines()], [r(p) for p in T(b'\n').splitlines()], [r(p) for p in T(b'a\r\nb').splitlines(keepends=True)])
    print([r(p) for p in s.partition(b',')], [r(p) for p in s.partition(b'zz')], [r(p) for p in s.rpartition(b'abc')], [r(p) for p in s.rpartition(b'zz')])
    print(r(T(b',').join([b'a', b'b', b'c'])), r(T(b',').join(())), r(T(b'--').join((b'x',))), r(T(b'').join([T(b'a'), memoryview(b'b')])))
    print(r(s.replace(b'abc', b'X')), r(s.replace(b'abc', b'X', 1)), r(s.replace(b'a', b'')), r(T(b'abc').replace(b'', b'-')),
          r(T(b'abc').replace(b'', b'-', 2)), r(T(b'').replace(b'', b'x')), r(s.replace(b'zz', b'y')), r(T(b'aaa').replace(b'aa', b'b')),
          r(T(b'aaa').replace(b'a', b'bb', 0)), r(T(b'aaa').replace(b'a', b'b', -1)))
    print(r(T(b'  x  ').strip()), r(T(b'  x  ').lstrip()), r(T(b'  x  ').rstrip()), r(T(b'xxhixx').strip(b'x')), r(T(b'xxhixx').lstrip(b'x')),
          r(T(b'xxhixx').rstrip(b'x')), r(T(b'abcba').strip(b'ab')), r(T(b'\t\n x \x0b').strip()), r(T(b'x').strip(b'')), r(T(b'xyx').strip(None)))
    print(r(T(b'prefix-body').removeprefix(b'prefix-')), r(T(b'body.suffix').removesuffix(b'.suffix')), r(T(b'body').removeprefix(b'x')),
          r(T(b'body').removesuffix(b'')))
    print(r(T(b'ab').center(7)), r(T(b'ab').center(7, b'*')), r(T(b'abc').center(6, b'*')), r(T(b'ab').center(1)), r(T(b'ab').ljust(5, b'.')),
          r(T(b'ab').rjust(5, b'.')), r(T(b'42').zfill(5)), r(T(b'-42').zfill(5)), r(T(b'+42').zfill(5)), r(T(b'abc').zfill(2)), r(T(b'').zfill(3)))
    print(r(T(b'a\tb\tc').expandtabs()), r(T(b'a\tb\tc').expandtabs(4)), r(T(b'a\tb\nc\td').expandtabs(3)), r(T(b'a\tb').expandtabs(0)),
          r(T(b'a\tb').expandtabs(-1)), r(T(b'ab\r\tc').expandtabs(4)))
    tbl = bytes.maketrans(b'abc', b'xyz')
    print(len(tbl), r(tbl[97:100]), r(T(b'aabbcc dd').translate(tbl)), r(T(b'aabbcc dd').translate(tbl, b'd ')), r(T(b'abc').translate(None, b'b')))
    print(s.hex(), s.hex(b':'), s.hex('-', 4), s.hex(':', -4), s.hex('.', 20), r(T.fromhex('48 65 6c6C')), r(T.fromhex(' 00ff ')))
    print(r(s.decode()), r(s.decode('ascii')), r(T(b'\xe9').decode('latin-1')), r(T(b'\xc3\xa9').decode('utf-8')),
          r(T(b'a\xffb').decode('utf-8', 'replace')), r(T(b'a\xffb').decode(errors='ignore')), r(T(b'a\xffb').decode(encoding='utf-8', errors='backslashreplace')))
    print(s == s, s == T(s), s != s, s < T(b'Z'), s <= s, s > T(b'A'), s >= T(b'Hello'), T(b'a') < T(b'ab'), T(b'') == T(b''), s == 'x', s.__eq__(1) is NotImplemented)
    print(r(s + b'!'), r(s + bytearray(b'?')), r(s + memoryview(b'#')), r(s * 2), r(2 * T(b'ab')), r(T(b'ab') * 0), r(T(b'ab') * -3), r(T(b'ab') * 1))
    print(r(T(b'abc') % ()), r(T(b'%d-%s') % (5, b'x')), r(T(b'%5s|%-5s|%.2s') % (b'ab', b'cd', b'efg')), r(T(b'%c%c') % (65, b'B')), r(T(b'%%')% ()),
          r(T(b'%x %X %o %#x %#o %05d %+d % d') % (255, 255, 8, 255, 8, 3, 3, 3)), r(T(b'%a %r') % ('é', 'x')), r(T(b'%b') % bytearray(b'z')))
    print(list(s[:3]), [x for x in T(b'\x00\xff')], list(T(b'')))
    it = iter(T(b'ab'))
    print(next(it), next(it), type(it).__name__)
    print(s.__len__(), s.__contains__(72), s.__getitem__(1), s.__sizeof__() > 0, bytes(s) == bytes(T(s)))

b = b'abc'
print(b.__hash__() == b.__hash__(), hash(b'') == 0, hash(b'a') != hash(b'b'), hash(b'abc') == hash(bytearray(b'abc').copy().__bytes__()) if False else True)
print(hash(b'abc') == hash(bytes(bytearray(b'abc'))))
print(bytes(), bytes(3), bytes(b'ab'), bytes(bytearray(b'cd')), bytes([1, 2, 3]), bytes((250, 5)), bytes(memoryview(b'xy')), bytes('é', 'utf-8'),
      bytes('é', 'latin-1'), bytes('a', 'ascii', 'strict'), bytes(range(5)), bytes(iter([1, 2])))
print(bytearray(), bytearray(3), bytearray(b'ab'), bytearray([1, 2]), bytearray('é', 'utf-8'), bytearray(memoryview(b'q')), bytearray(range(3)))
print(int.from_bytes(b'\x01\x02', 'big'), int.from_bytes(bytearray(b'\x01\x02'), 'little'), int.from_bytes([255, 255], 'big', signed=True),
      int.from_bytes(memoryview(b'\x10'), 'big'), int.from_bytes((1, 0), byteorder='little'), int(b'42'), int(bytearray(b' 7 ')), int(memoryview(b'-9')))
print(str(b'abc', 'ascii'), str(bytearray(b'abc'), 'utf-8'), str(memoryview(b'xyz'), 'latin-1'))

class B(bytes):
    pass
x = B(b'subclass')
print(r(x), type(x).__name__, x == b'subclass', hash(x) == hash(b'subclass'), r(x.upper()), type(x.upper()).__name__, len(x), r(x[1:3]), type(x[1:3]).__name__)
class BA(bytearray):
    pass
y = BA(b'sub')
y.append(33)
print(r(y), type(y).__name__, y == b'sub!', r(y.copy()), type(y.copy()).__name__, r(y + b'x'), type(y + b'x').__name__)
class WithBytes:
    def __bytes__(self):
        return b'custom'
print(bytes(WithBytes()), bytes(B(b'z')), type(bytes(B(b'z'))).__name__)
