"""bytes + bytes in hot loops (empty sides, in place, a bytearray or a
memoryview on either side, the error with a str), as pickle builds its
opcodes."""
import struct
from support import loops


def run():
    out = []
    acc = b''
    for i in range(400):
        head = b'X' if i & 1 else b''
        acc = (head + struct.pack('<B', i & 0xff) + b'tail')[:12]
        acc += b'!'
        out.append(acc)
    res = [len(out), out[:5], bytearray(b'a') + b'b', b'a' + bytearray(b'b'), b'a' + memoryview(b'm')]
    try:
        b'a' + 'b'
    except TypeError as e:
        res.append(str(e))
    return res


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
