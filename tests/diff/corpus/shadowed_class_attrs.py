"""Instance attributes that shadow a class attribute which is no
descriptor (BytesIO's _buffer = None), in hot loops: the instance's value,
then the class's once the instance's is deleted, then a new class value,
on instances that keep a real dict (a __dict__ read)."""
from support import loops


class Buf:
    _buffer = None
    _pos = 0

    def __init__(self):
        self.__dict__.update({'_buffer': bytearray(b'abc')})
        self._pos = 1

    def size(self):
        return (len(self._buffer) if self._buffer is not None else -1) + self._pos


def run():
    b = Buf()
    out = 0
    for i in range(300):
        out += b.size()
    del b._buffer
    after = [b.size() for _ in range(50)]
    Buf._buffer = bytearray(b'zz')
    later = [b.size() for _ in range(50)]
    Buf._buffer = None
    return out, after[:3], later[:3], vars(b)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
