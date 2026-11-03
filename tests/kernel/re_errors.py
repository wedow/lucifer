# re.error messages and positions, and the compile-time checks of the parser.
import re

PATTERNS = ['(', ')', '(?', '(?P', '(?P<', '(?P<a', '(?P<1>a)', '(?P<a>a)(?P<a>b)', '(?P=x)', '(?P=a', '(?P>a)', '*', 'a**', 'a{2,1}',
            'a{99999999999}', '[', '[a', '[b-a]', '[\\d-z]', '\\', '\\1', '(a)\\2', '\\g', '(?<a)', '(?<=a*)b', '(?<!a|bc)d',
            '(?(1)a)', '(a)(?(2)b)', '(?(', '(?(a', '(?(1)a|b|c)', '\\x', '\\xg', '\\u12', '\\U0011ffff',
            '(?z)', '(?i', '(?-', '(?i-i:a)', '(?ii)', '(?a)(?u)', '(?L)', '(?x', 'a\\', '\\8', '\\08', '\\777', '\\400', '(?#', '(?#abc',
            '(?:', '(?>', '(?P<a>', 'a{', 'a{1', 'a{1,', 'a{,}', 'a{1,2', '{1}', '+a', '?', '(*)', '(?i:', '[]', '[^]', '[]a]', '[\\w-a]',
            '\\b+', '^*', '$+', '(?=a)*', '(?<=a)+', 'a++', 'a*+', '(?:a)++', '\\Z{2}', '\\z', '\\s\\S\\w\\W\\d\\D', '\\0', '\\a\\f\\v\\t\\r\\n',
            '(?x) a b # c\n d', 'x{1,2}{3}', 'a|*', '|', '||', '(|)', '(?|a)', '(?P<a>x)(?(a)y|z)', '(?P<é>a)', '(?P<a b>x)']
for src in PATTERNS:
    for flag in (0, re.X):
        try:
            p = re.compile(src, flag)
            print(repr(src), flag, 'ok', p.groups, sorted(p.groupindex.items()))
        except re.error as e:
            print(repr(src), flag, 'error', e.msg, e.pos, e.lineno, e.colno, str(e))
        except Exception as e:
            print(repr(src), flag, type(e).__name__, e)
for src in (b'(', b'[a', b'\\xg', b'(?u)a', b'\\u1234', b'(?P<a>b)(?P=b)', b'\xe9\\', b'a\nb(\n'):
    try:
        re.compile(src)
        print(src, 'ok')
    except re.error as e:
        print(src, 'error', e.msg, e.pos, e.lineno, e.colno, str(e))
try:
    re.compile('a\nb\nc(\n')
except re.error as e:
    print(e.msg, e.pos, e.lineno, e.colno, e)
try:
    re.compile('a', 1 << 40)
except Exception as e:
    print(type(e).__name__, e)
for flags in (re.A | re.U, re.A | re.L, re.L, 1 << 10):
    try:
        print(re.compile('a' if flags != re.L else b'a', flags).flags)
    except Exception as e:
        print(type(e).__name__, e)
try:
    re.compile(b'a', re.U)
except Exception as e:
    print(type(e).__name__, e)
try:
    re.compile('a', re.L)
except Exception as e:
    print(type(e).__name__, e)
try:
    re.compile(re.compile('a'), re.I)
except Exception as e:
    print(type(e).__name__, e)
print(re.compile(re.compile('a')) is re.compile(re.compile('a')))
try:
    re.compile(5)
except Exception as e:
    print(type(e).__name__, e)
print(re.error.__module__, re.PatternError is re.error, re.error('x', 'ab', 1).msg)
