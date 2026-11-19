# BytesIO.write of each kind of bytes-like object: at the end, over the
# middle, past the end (padded with null bytes), empty, of a non-byte
# format, while a view is exported, closed, and of what is no buffer.
import io
from array import array

f = io.BytesIO(b'0123456789')
print(f.write(b'ab'), f.getvalue(), f.tell())
f.seek(8)
print(f.write(bytearray(b'XYZ')), f.getvalue())
f.seek(15)
print(f.write(memoryview(b'..mv..')[2:4]), f.getvalue())
print(f.write(b''), f.write(bytearray()), f.tell())
print(f.write(array('i', [1, 2])), len(f.getvalue()), f.tell())


class Bytes(bytes):
    pass


print(f.write(Bytes(b'sub')), f.getvalue()[-3:])
total = io.BytesIO()
for i in range(2000):
    total.write(b'%d,' % i)
    if i % 7 == 0:
        total.write(bytearray(b';'))
data = total.getvalue()
print(len(data), data[:20], data[-20:], total.tell())
view = total.getbuffer()
try:
    total.write(b'more')
except BufferError as e:
    print('BufferError', e)
view.release()
print(total.write(b'more'), total.getvalue()[-6:])
for bad in ('text', 5, None, [1]):
    try:
        f.write(bad)
    except TypeError as e:
        print(type(e).__name__, e)
f.close()
for b in (b'x', b'', bytearray(b'y'), memoryview(b'z')):
    try:
        f.write(b)
    except ValueError as e:
        print(type(e).__name__, e)
