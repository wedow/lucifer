# str.format, format_map, the format-spec mini-language, int.__format__ and %-formatting.
class Pt:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def __repr__(self):
        return 'Pt(%d, %d)' % (self.x, self.y)
    def __getitem__(self, i):
        return self.x if i == 0 else self.y

class Mapping:
    def __getitem__(self, key):
        if key == 'missing':
            raise KeyError(key)
        return key.upper()

print('{} {}'.format(1, 2), '{0} {1} {0}'.format('a', 'b'), '{} {}'.format(*(3, 4)), '{a} {b}'.format(a=1, b=2), '{0}{a}'.format('x', a='y'))
print('{:5}|{:<5}|{:>5}|{:^5}|{:*^7}|{:05}|{:+}|{: }|{:-}'.format(1, 2, 3, 4, 5, 6, 7, 8, 9))
print('{!r} {!s} {!a}'.format('é', 'é', 'é'), '{0!r:>8}'.format('x'), '{0.x} {0.y} {0[0]} {0[1]}'.format(Pt(1, 2)))
print('{{}} {{{}}}'.format(5), '}}{{'.format(), '{:{}}'.format(5, 4) + '|', '{:{}.{}}'.format('abcdef', 8, 3) + '|', '{0:{1}}'.format('a', 3) + '|')
print('{a.x}'.format(a=Pt(7, 8)), '{p[1]}'.format(p=Pt(7, 8)))
print('{[a]}'.format({'a': 1}), '{k[a]}'.format(k={'a': 2}))
print('{x} {y}'.format_map(Mapping()))
print('{:d} {:x} {:X} {:o} {:b} {:c} {:n}'.format(255, 255, 255, 255, 255, 65, 255))
print('{:#x} {:#X} {:#o} {:#b} {:+#x} {:#010x} {:<#10x}| {:^#10x}|'.format(255, 255, 8, 5, 255, 255, 255, 255))
print('{:,} {:_} {:,d} {:_d} {:_x} {:_o} {:_b} {:,}'.format(10 ** 9, 10 ** 9, -1234, 1234, 0xdeadbeef, 0o7777777, 0b11110000, 0))
print('{:010,} {:010_} {:<010,}| {:^10,}| {:+012,} {:*>12,}'.format(1234567, 1234567, 1234567, 1234567, 1234567, 1234567))
print('{:10}|{:<10}|{:>10}|{:^10}|{:.2}|{:10.2}|{:.0}|'.format('abc', 'abc', 'abc', 'abc', 'abc', 'abc', 'abc'))
print('{:s}'.format('x'), '{:€^7}'.format('x'), '{:\U0001f600<4}'.format('y'), '{:>3}'.format('€uro'))
print(format(5, ''), format(5, 'd'), format(5, '3d'), format('s', ''), format('s', '3'), format(True, ''), format(True, 'd'), format(None, ''))
print(format(2 ** 100, 'x'), format(-2 ** 100, 'X'), format(2 ** 100, '_'), format(10 ** 30, ','), format(-5, '05'), format(-5, '=5'), format(-5, '<5') + '|')
print((5).__format__(''), (5).__format__('04'), (-5).__format__('x'), 'abc'.__format__('>5'), 'abc'.__format__(''), True.__format__('5') + '|')
print(format(Pt(1, 2), ''))

# %-formatting
print('%s %s' % ('a', 'b'), '%s' % 'x', '%s' % (1,), '%d' % 5, '%i' % 5, '%u' % 5, '%5d|' % 5, '%-5d|' % 5, '%05d' % 5, '%+d' % 5, '% d' % 5, '%+d' % -5)
print('%x' % 255, '%X' % 255, '%#x' % 255, '%#X' % 255, '%o' % 8, '%#o' % 8, '%x' % -255, '%#x' % -255, '%08x' % 255, '%-8x|' % 255, '%.4x' % 255, '%8.4x|' % 255)
print('%5.3d|' % 5, '%05.3d|' % 5, '%-8.3d|' % -5, '%.0d' % 0, '%+.3d' % 5, '%d' % 12345678901234567890123, '%x' % (2 ** 70), '%d' % True)
print('%s' % None, '%r' % 'a', '%a' % 'é', '%s' % 'é', '%r' % 'é', '%5s|' % 'ab', '%-5s|' % 'ab', '%.1s' % 'ab', '%5.1s|' % 'ab', '%.10s' % 'ab')
print('%c' % 65, '%c' % 'x', '%c' % 0x20ac, '%5c|' % 'x', '%-5c|' % 66)
print('%%', '%d%%' % 50, '100%%' % ())
print('%(a)s %(b)d' % {'a': 'x', 'b': 3}, '%(a)s' % {'a': 1}, '%(a)5s|' % {'a': 'x'}, '%(a)s %(a)s' % {'a': 'z'})
print('%*d|' % (5, 42), '%-*d|' % (5, 42), '%.*s' % (2, 'abcdef'))
print('%s %s' % (Pt(1, 2), 3), '%r' % Pt(3, 4), '%s' % (Pt(5, 6),))
print('%ld %hd %Ld' % (1, 2, 3), '%x' % 255, '%s%s' % ('a', 'b'))
