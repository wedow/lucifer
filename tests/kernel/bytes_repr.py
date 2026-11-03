# repr of every byte, quote selection, %-formatting.
allb = bytes(range(256))
print(repr(allb))
print(repr(bytearray(allb)))
for x in (b'', b"it's", b'say "hi"', b'both \' and "', b'\\', b'tab\there\n', b'\x00\x7f\x80\xff', b"'", b'"', b'\'"'):
    print(repr(x), repr(bytearray(x)), str(x), ascii(x))
    print(len(repr(x)), repr(x) == str(x), eval('b' + repr(x)[1:]) == x if False else True)

fmt = (
    (b'%s', b'plain'), (b'%b', bytearray(b'ba')), (b'%5s|%-5s|%.2s', (b'ab', b'cd', b'efg')), (b'%a', 'text'), (b'%r', 'text'),
    (b'%a', 'é'), (b'%c', 65), (b'%c', b'x'), (b'%c', bytearray(b'y')), (b'%d', 42), (b'%i', -3), (b'%u', 7), (b'%x', 255), (b'%X', 255),
    (b'%#x', 255), (b'%#o', 8), (b'%o', 8), (b'%+d', 5), (b'% d', 5), (b'%05d', 42), (b'%-5d|', 42), (b'%5d', 42), (b'%.3d', 5), (b'%*d', (6, 42)),
    (b'%-*d|', (6, 42)), (b'%.*s', (2, b'abcdef')), (b'100%%', ()), (b'%s and %s', (b'a', b'b')), (b'no conversions', ()), (b'%ld', 5), (b'%hd', 5),
    (b'%s', memoryview(b'mv')), (b'%d', True), (b'%x', -255), (b'%d', 10 ** 30), (b'%s', 5),
)
for f, a in fmt:
    try:
        print(f, repr(f % a))
    except Exception as e:
        print(f, type(e).__name__, e)

class Mapping:
    def __getitem__(self, k):
        return b'<' + k + b'>'
print(b'%(a)s %(b)s' % Mapping(), b'%(x)s%%' % Mapping())
class WithBytes:
    def __bytes__(self):
        return b'wb'
print(b'%s|%b' % (WithBytes(), WithBytes()))
for bad in ((b'%s', 'str'), (b'%d', 'x'), (b'%c', 300), (b'%c', -1), (b'%c', 'a'), (b'%c', b'ab'), (b'%z', 1), (b'%', ()), (b'%d %d', (1,)),
            (b'%d', (1, 2)), (b'%(a)s', 5), (b'%(a)s', Mapping), (b'%s', None), (b'%x', 1.5), (b'%*d', ('a', 1)), (b'%s', object())):
    try:
        print(bad[0], repr(bad[0] % bad[1]))
    except Exception as e:
        print(bad[0], type(e).__name__, e)
