# Hot patterns: each is used enough that the engine specializes it partway
# through, so the later rounds must reproduce the first. Every construct the
# specializer compiles appears here, with subjects that make it backtrack.
import re
import subprocess
import sys

PATTERNS = [
    # literals, prefixes, sets, first characters
    ('Python', 0), ('Python|Perl', 0), ('(Python|Perl|Tcl)', 0), ('[Pp]ython', 0), ('x[^y]z', 0), (r'\d\d-\d', 0),
    # alternatives: exclusive first characters, a shared tail, and backtracking inside one
    ('(?:ab|cd|e)f', 0), ('(?:a|ab)(?:c|bcd)(d*)', 0), ('(?:a+b|a)c', 0), ('(x)?(?:y|x)z', 0), ('(?:|a)b', 0),
    # one-character repeats: greedy, lazy, possessive, counted, given back to the tail
    ('a*', 0), ('.*Python', 0), ('.*P.*y.*t.*h.*o.*n.*', 0), (r'\w+\s', 0), ('a*?b', 0), ('[ab]*?b', 0), ('a{2,3}', 0),
    ('a{2,3}?', 0), ('[^,]*+,', 0), ('x{3}', 0), (r'(\w+)\s+(\w+)', 0), (r'\s*(\S*)\s*$', 0), ('a+?$', 0),
    # repeats of groups: plain loops, loops that back off, lazy loops, optional groups
    ('([0a-z][a-z0-9]*,)+', 0), ('(?:[a-z][a-z0-9]*,)+x', 0), ('(?:ab)*abc', 0), ('(a|b)*c', 0), ('(?:ab)*?c', 0),
    (r'(?:\\.|[^"\\])*"', 0), (r'(\d+)(?:\.(\d+))?', 0), ('(a(b)?)+', 0), ('(?:(a)|b){2,3}', 0), ('(?:a|b){0,2}?c', 0),
    (r'(<(.+?)(\s.*?)?>)', 0), ('(?:x*)*y', 0), ('(?:a?)*?b', 0),
    # repeats whose iterations backtrack: greedy, lazy, counted, nested, empty iterations, in lookarounds
    ('(?:a|ab)*c', 0), ('(<(.|\\s)+>)', 0), (r'((?:(?!_)\w)+(?:[-._+:](?:(?!_)\w)+)*)::', 0), ('(?:a|ab)*?c', 0),
    ('(?:(a)|ab){2,}?c', 0), ('(?:a|ab){1,3}c', 0), ('(?:(a)|b(a)?){2}', 0), ('(?:(?:a|ab)+b)*c', 0),
    ('((?:a|ab)*(c|d))*e', 0), ('(x)((?:a|ab)*)(c)', 0), ('(?:a|)*b', 0), ('(?:|a)*?b', 0), ('(?=(?:a|ab)*c)a', 0),
    ('(?:(b)|a(?:b|))*?c', 0), ('(?:(a|ab)(c|bcd))*(d*)', 0),
    # atomic groups, possessive groups, lookarounds
    ('(?>a|ab)c', 0), ('(?>a+)b', 0), ('(?:ab|a)*+c', 0), ('(?:a|b)++', 0), ('(?=ab)a', 0), ('(?!ab)a.', 0),
    ('(?<=a)b', 0), ('(?<!a)b', 0), ('(?=(a+))a*b\\1', 0), ('a(?!b|c)', 0), ('(?<=(a))(b)', 0),
    # backreferences, conditionals
    (r'(a+)\1', 0), (r'(?i)(a+)\1', 0), (r'(\w)\1', 0), ('(a)?(?(1)b|c)', 0), ('(a)?(?(1)b+|c)d', 0),
    (r'(?P<q>["\'])(.*?)(?P=q)', 0),
    # anchors and boundaries
    ('^ab', 0), ('^ab', re.M), ('ab$', 0), ('ab$', re.M), (r'\Aab\Z', 0), (r'\bab\b', 0), (r'\Bb\B', 0), (r'^\s*$', re.M),
    # case-insensitive and Unicode
    ('hello', re.I), ('[a-z]+', re.I), ('stra\xdfe', re.I), ('k', re.I), (r'\w+', 0), (r'\w+', re.A), ('[Ѐ-ӿ]+', 0),
    (r'\d+', 0), (r'\s+', 0), ('.', re.S),
]
SUBJECTS = ['', 'a', 'ab', 'abc', 'aab', 'abab', 'ababc', 'aaab', 'Python Perl', 'xx Tcl yy', 'abcd',
            'a b  c', 'ab1,cd,x', '"a\\"b"', 'xyz xaz', '12-3 45', 'hello World', 'Hello\nab\n',
            'STRASSE stra\xdfe', 'K k K', 'жук ab', '<a href> <b>', '1.25', 'aa aa', "'q' \"r\"", 'b',
            'cd ef', 'abbc', 'xxxy', 'x', ' \n', 'aabababc', 'xababce', 'ab_c-d::', '<a\nb>c>', 'abacbcdd']


def results(p, s):
    out = []
    for m in (p.search(s), p.match(s), p.fullmatch(s), p.search(s, 1), p.match(s, 1, 3)):
        out.append(None if m is None else (m.span(), m.groups(), m.lastindex))
    out.append(p.findall(s))
    out.append([m.span() for m in p.finditer(s)])
    out.append(p.split(s))
    out.append(p.subn('<\\g<0>>', s))
    return out


for src, flags in PATTERNS:
    p = re.compile(src, flags)
    first = [results(p, s) for s in SUBJECTS]
    later = [[results(p, s) for s in SUBJECTS] for _ in range(4)]
    print(repr(src), flags, all(r == first for r in later))
    print(' ', first)

# bytes, a wide subject, long subjects
for src in (b'(a|b)*c', b'[^,]*,', b'(?i)AB', rb'\w+', b'(?<=x)y'):
    p = re.compile(src)
    rows = [[p.findall(s), p.sub(b'-', s)] for s in (b'abac,', b'xyab,AB', b'\xe9\xff ab', b'xy') for _ in range(40)]
    print(src, all(r == rows[i - i % 40] for i, r in enumerate(rows)), rows[::40])
wide = 'ж\U0001f600a' * 50
for src in ('[Ѐ-ӿ]\\W', '(?:ж.)*', '\U0001f600a', '.a$'):
    p = re.compile(src)
    print(repr(src), [(len(p.findall(wide)), p.search(wide).span()) for _ in range(60)][-1])
p = re.compile('(?:ab)*c')
print([p.match('ab' * n + 'c').span() for n in (0, 1, 500)], re.match('a*?b', 'a' * 3000 + 'b').span())
# iterations that backtrack, more of them than the stack holds frames
p = re.compile('(?:a|ab)*c')
print([[p.match('ab' * n + 'c').span(), p.search('x' + 'ab' * n + 'c').span()] for n in (0, 1, 500, 20000)])
p = re.compile('(?:a|ab)*?c')
print([p.match('ab' * n + 'c').span() for n in (0, 1, 500, 20000) for _ in range(3)])

# Every pattern above compiled at its first use and checked against the interpreter on each call.
CHILD = '''
import re
for src, flags in %r:
    p = re.compile(src, flags)
    print([[p.search(s), p.fullmatch(s), p.findall(s), p.split(s)] for s in %r] is not None, end=' ')
print()
''' % (PATTERNS, SUBJECTS)
child = subprocess.run([sys.executable, '-X', 'lucifer_stress=sre', '-c', CHILD], capture_output=True, text=True,
                       timeout=600)
print(child.returncode, child.stdout.count('True'), child.stderr[-2000:])
