# The seeded differential again over characters beyond Latin-1 (the engine
# reads code points through a wrapper there), with case folding.
import re

seed = 777


def rnd(n):
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return (seed >> 8) % n


ATOMS = ['a', 'k', '\u212a', 'K', '\u0436', '\u0416', '.', '\\w', '\\d', '\\s', '\\W', '\\D', '[a\u0436]', '[^\u0436]', '[\u0400-\u04ff]',
         '[^a-z]', '\\b', '\\B', '^', '$', '', '\u0130', 'i', 'I', '\u03c3', '\u03c2', '\u03a3', '\U0001f600', '[\U0001f600-\U0001f64f]',
         '\u0663', '\xdf', '\u1e9e', 'ss', ' ', '\u2003', '[\\W\\d]', '[\u0100-\U0010ffff]']
QUANTS = ['', '', '', '*', '+', '?', '*?', '+?', '??', '{2}', '{1,2}', '{0,2}?', '{,1}', '*+', '++', '?+']


def gen(depth):
    n = rnd(4) + 1
    out = ''
    while n > 0:
        n -= 1
        k = rnd(11) if depth > 0 else rnd(4)
        if k < 4:
            item = ATOMS[rnd(len(ATOMS))]
        elif k == 4:
            item = '(' + gen(depth - 1) + ')'
        elif k == 5:
            item = '(?:' + gen(depth - 1) + ')'
        elif k == 6:
            item = '(?:' + gen(depth - 1) + '|' + gen(depth - 1) + ')'
        elif k == 7:
            item = ['(?=', '(?!', '(?<=', '(?<!', '(?>'][rnd(5)] + gen(depth - 1) + ')'
        elif k == 8:
            item = '(' + gen(depth - 1) + '|' + gen(depth - 1) + ')'
        elif k == 9:
            item = ['\\1', '\\2', '(?P=g1)', '(?(1)' + gen(depth - 1) + '|' + gen(depth - 1) + ')', '(?(2)' + gen(depth - 1) + ')',
                    '(?i:' + gen(depth - 1) + ')', '(?-i:' + gen(depth - 1) + ')', '(?s:.)', '(?m:^)'][rnd(9)]
        else:
            item = '(?P<g' + str(rnd(3)) + '>' + gen(depth - 1) + ')'
        q = QUANTS[rnd(len(QUANTS))]
        if item in ('^', '$', '\\b', '\\B', '\\A', '\\Z', ''):
            q = ''
        out += item + q
    return out


POOL = 'aKk\u212a\u0436\u0416\u0130iI\u03c3\u03c2\u03a3\U0001f600\u0663 \xdf\u1e9e\u2003_1\n'


def subject():
    n = rnd(9)
    s = ''
    while n > 0:
        n -= 1
        s += POOL[rnd(len(POOL))]
    return s


def mt(m):
    if m is None:
        return None
    return (m.span(), m.groups(), m.lastindex, m.lastgroup, m.regs)


FLAGS = [0, 0, re.I, re.I, re.I | re.M, re.S, re.A, re.I | re.A]
i = 0
while i < 120:
    i += 1
    src = gen(2)
    flags = FLAGS[rnd(len(FLAGS))]
    try:
        p = re.compile(src, flags)
    except re.error as e:
        print(i, repr(src), 'error', e.msg, e.pos)
        continue
    except Exception as e:
        print(i, repr(src), type(e).__name__, e)
        continue
    print(i, repr(src), flags, p.groups, sorted(p.groupindex.items()))
    j = 0
    while j < 4:
        j += 1
        s = subject()
        pos = rnd(4)
        endpos = rnd(10)
        print(' ', repr(s), pos, endpos)
        print('  ', mt(p.match(s)), mt(p.search(s)), mt(p.fullmatch(s)))
        print('  ', mt(p.search(s, pos)), mt(p.match(s, pos, endpos)), mt(p.fullmatch(s, pos, endpos)), mt(p.search(s, pos, endpos)))
        print('  ', p.findall(s), p.findall(s, pos, endpos))
        print('  ', p.split(s), p.sub('<\\g<0>>', s), p.subn('-', s, count=2))
        print('  ', [m.span() for m in p.finditer(s)])
