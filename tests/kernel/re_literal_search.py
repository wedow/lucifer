# The engine's literal searches: a pattern's literal prefix found in bytes,
# Latin-1 and wide subjects within pos and endpos, and the scans of `.` and of
# a negated literal for the next newline or character.
import re

subjects = [
    'abcabcab' * 3 + 'Xyz-end',
    ('lorem ipsum ' * 200) + 'needle! and more ' + ('dolor ' * 100) + 'needle?',
    'caf\xe9 caf\xe9 na\xefve r\xe9sum\xe9 caf\xe9!',
    'жжxжy  needle \U0001f600needleж tail\nnext lineж',
    'line one\nline two\n\nline four with needle\nlast',
]
patterns = ['needle', 'needle[!?]', 'abc', 'cab', 'Xyz', 'caf\xe9', '\xe9', 'r\xe9s', 'жy', 'ж',
            '\U0001f600needle', 'line \\w+', 'e.*e', 'n.*?e', '[^\\n]*needle', '^line', 'x[^ж]*',
            'with needle\\nl', 'zzz', ' needle', 'tail\\n.*']
for s in subjects:
    n = len(s)
    for p in patterns:
        r = re.compile(p)
        print(p.encode('unicode_escape'), [m.span() for m in r.finditer(s)][:6], len(r.findall(s)))
        for pos, endpos in ((0, n), (1, n - 1), (n // 2, n), (0, n // 2), (3, 3), (n - 7, n), (n, n)):
            m = r.search(s, pos, endpos)
            print(' ', pos, endpos, m and m.span())
    if s.isascii() or max(s) < 'Ā':
        b = s.encode('latin-1')
        for p in patterns:
            try:
                rb = re.compile(p.encode('latin-1'))
            except UnicodeEncodeError:
                continue
            spans = [m.span() for m in rb.finditer(b)][:6]
            print(b'b', p.encode('unicode_escape'), spans, rb.search(bytearray(b), 2, len(b) - 2) is not None,
                  rb.search(memoryview(b), 5) and rb.search(memoryview(b), 5).span())
print(re.sub('needle', 'N', subjects[1]).count('N'), re.subn(b'ab', b'', subjects[0].encode())[1])
print(re.search('жneedle', 'x' * 2000 + 'жneedle'), re.search('Ā', 'abc\xff'))
print(re.compile('ab').search('xxab', 0, 3), re.compile('ab').search('xxab', 0, 4).span())
