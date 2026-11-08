# A buffered read with a size of the wrong type raises before it changes the
# stream: the next read still works.
import io

b = io.BufferedReader(io.BytesIO(b'abcdef'))
for size in (0.0, '1', 1.5):
    try:
        b.read(size)
    except TypeError as e:
        print(type(size).__name__, e)
print(b.read(0), b.read(2), b.read(None), b.read(-1))
try:
    io.BufferedReader(io.BytesIO(b'')).read(-2)
except ValueError as e:
    print(e)
