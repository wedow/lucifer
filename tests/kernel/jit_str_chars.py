# Hot reads of str characters (s[i], negative too) and compares of a str
# with one character the code names, both ways round (==, !=, <, >= and
# the reflected order), over ASCII, latin-1, BMP and astral strs, the
# empty str and strs longer than one character; and the same sites given
# indexes out of range, a str subclass and a str of another width.
# Each result must be the interpreter's; tools/jitcheck also runs this at
# two sizes and requires that compiled code's calls of the str primitives
# do not grow with the iterations: they are read and compared in line.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

TEXTS = ['{"a": [1, 2.5, "x"]}', 'caf\xe9 cr\xe8me', 'grüße 中文', 'emoji \U0001f600!', 'a']


def scan(s, n):
    counts = [0, 0, 0, 0, 0, 0]
    m = len(s)
    for k in range(n):
        c = s[k % m]
        d = s[-1 - k % m]
        if c == '"':
            counts[0] += 1
        if '{' == c:
            counts[1] += 1
        if c != ' ':
            counts[2] += 1
        if c < 'a':
            counts[3] += 1
        if 'm' >= d:
            counts[4] += 1
        counts[5] += ord(c) & 7
    return counts


def pick(s, i):
    return s[i]


def same(s, t):
    n = 0
    if s == 'x':
        n += 1
    if 'x' == s:
        n += 2
    if s < 'x':
        n += 4
    if 'x' < s:
        n += 8
    if s >= 'x':
        n += 16
    if s != 'x':
        n += 32
    return n


for t in TEXTS:
    print(repr(t), scan(t, N))
print([pick(t, 0) + pick(t, -1) for t in TEXTS])


class S(str):
    pass


def attempt(fn, *args):
    try:
        return repr(fn(*args))
    except Exception as e:
        return '%s: %s' % (type(e).__name__, e)


for _ in range(N):
    pick('xyz', 1)
    same('w', 'x')
for args in (('xyz', 3), ('xyz', -4), ('xyz', 1.0), (S('xyz'), 1), ('中文', 1),
             ('\U0001f600', 0), ('', 0)):
    print('pick', attempt(pick, *args))
for s in ('', 'x', 'xa', 'w', 'y', 'xx', '\xe9', '中', '\U0001f600', S('x'), 'X'):
    print('same', repr(s), bin(same(s, 'x')))
