# _sre on its own: constants, case helpers, code validation and the objects' refusals.
import re
import _sre
from re import _constants as C

print(_sre.MAGIC == C.MAGIC, _sre.getcodesize(), _sre.CODESIZE, _sre.MAXREPEAT, _sre.MAXGROUPS)
print([_sre.ascii_tolower(c) for c in (65, 90, 97, 200, 0x130)], [_sre.unicode_tolower(c) for c in (65, 0xc0, 0x130, 0x3a3, 0x1e9e, 0x212a, 0x10400, 0x1f600)])
print([_sre.ascii_iscased(c) for c in (65, 48, 200)], [_sre.unicode_iscased(c) for c in (65, 48, 0xdf, 0x130, 0x3c2, 0x1f600, 0x2160)])
sweep = 0
for c in range(0x30000):
    sweep = (sweep * 31 + _sre.unicode_tolower(c) + 7 * _sre.unicode_iscased(c)) % 1000000007
print(sweep)

ok = [C.INFO, 4, 0, 0, 1, C.LITERAL, 97, C.SUCCESS]
print(_sre.compile('a', 0, ok, 0, {}, ()).match('a'))
for bad in ([], [C.LITERAL, 97], [C.MARK, 0, C.SUCCESS], [C.REPEAT_ONE, 99, 0, 1, C.SUCCESS], [C.AT, 99, C.SUCCESS],
            [C.IN, 5, C.LITERAL, 1, C.SUCCESS], [C.GROUPREF, 0, C.SUCCESS], [C.BRANCH, 0, C.SUCCESS], [1000, C.SUCCESS]):
    try:
        _sre.compile('a', 0, bad, 0, {}, ())
        print('accepted', bad)
    except Exception as e:
        print(type(e).__name__, e)
for args in ((None, 0, [], 0, {}, ()), ('a', 0, (), 0, {}, ()), ('a', 0, [-1], 0, {}, ()), ('a', 0, [1 << 32], 0, {}, ()),
             (5, 0, ok, 0, {}, ()), ('a', 0, ok, 0, [], ())):
    try:
        _sre.compile(*args)
    except Exception as e:
        print(type(e).__name__, e)

p = re.compile('(a)')
m = p.match('a')
for cls in (type(p), type(m), type(p.scanner('a'))):
    try:
        cls()
    except TypeError as e:
        print(e)
print(type(p).__module__, type(p).__name__, type(m).__module__, type(m).__name__, type(p.scanner('a')).__name__)
print(p.groupindex == {}, type(re.compile('(?P<x>a)').groupindex).__name__, type(p).__class_getitem__(str))
try:
    iter(m)
except TypeError:
    print('not iterable')
print(re.compile('(?P<x>a)(?P<y>b)?').match('a').lastgroup, re.compile('(a)').match('a').lastgroup)

# Code words are read by value, as C's unsigned code array: an int subclass's
# comparisons never run (re._compiler emits int subclasses).
class Liar(int):
    def __eq__(self, other): return True
    def __ne__(self, other): return False
    def __lt__(self, other): return True
    def __le__(self, other): return True
    def __gt__(self, other): return True
    def __ge__(self, other): return True
    __hash__ = int.__hash__
words = [Liar(w) for w in (C.INFO, 4, 0, 1, 1, C.IN, 5, C.RANGE, 98, 99, C.FAILURE, C.LITERAL, 97, C.SUCCESS)]
liar = _sre.compile('[bc]a', 0, words, 0, {}, ())
print(liar.match('ba'), liar.match('xa'), liar.search('zzca'), liar.fullmatch('cb'))


class S(str):
    pass


m = re.compile('(b)').search(S('abc'))
print(m.string.__class__.__name__, m.group(1).__class__.__name__, m[0], m.pos, m.endpos)
mv = re.compile(b'b').search(memoryview(b'abc'))
print(mv, re.compile(b'b').search(bytearray(b'abc')).group().__class__.__name__)
sc = re.compile('a').scanner('aaa')
print([sc.search().span() for _ in range(4) if True][:3])

