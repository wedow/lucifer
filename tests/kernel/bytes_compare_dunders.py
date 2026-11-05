class B(bytes):
    pass


class Buf:
    def __buffer__(self, flags):
        return memoryview(b'a')


a, ba, mv = b'a', bytearray(b'a'), memoryview(b'a')
for left in (a, B(b'a'), ba):
    print([getattr(left, name)(other) for name in ('__eq__', '__ne__', '__lt__', '__ge__')
           for other in (a, ba, mv, Buf(), 'a', 5)])
print(a == ba, ba == a, a == mv, mv == a, a < bytearray(b'b'), bytearray(b'b') > a, B(b'x') == b'x')
print(bytes.__eq__(B(b'a'), a), bytes.__lt__(a, B(b'b')))
