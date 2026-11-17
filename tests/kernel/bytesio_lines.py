# BytesIO line reading: readline with and without a size, readlines with a
# hint, iteration, the end of the buffer, a position past it, bad arguments.
import io

b = io.BytesIO(b'one\ntwo\n\nthree')
print(b.readline(), b.readline(2), b.readline(None), b.readline(-1))
print(b.readline(0), b.readline(), b.readline(), b.tell())
b.seek(100)
print(b.readline(), list(b))
b.seek(0)
print(list(b))
b.seek(0)
print(b.readlines(), b.readlines())
for hint in (None, 0, -1, 1, 4, 5, 8):
    b.seek(0)
    print(hint, b.readlines(hint))
b.seek(4)
print(next(b), b.read(1), next(b), b.readline())
print(list(io.BytesIO(b'')), list(io.BytesIO(b'\n\n')), io.BytesIO(b'x\r\ny').readlines())

for args in ((1.5,), ('1',)):
    for name in ('readline', 'readlines'):
        try:
            getattr(io.BytesIO(b'a\nb'), name)(*args)
        except TypeError as e:
            print(name, args, e)

b.close()
for f in (b.readline, b.readlines, lambda: next(b), lambda: iter(b)):
    try:
        f()
    except ValueError as e:
        print(e)

# A size whose __index__ closes the file is converted before the closed
# check, as CPython's C BytesIO does.
class Closer:
    def __index__(self):
        c.close()
        return -1
for name in ('read', 'read1', 'readline'):
    c = io.BytesIO(b'a\nb')
    try:
        print(name, getattr(c, name)(Closer()))
    except ValueError as e:
        print(name, e)
try:
    io.BytesIO(b'a').read(1.5)
except TypeError as e:
    print('read', e)
