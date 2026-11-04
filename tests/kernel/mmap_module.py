import mmap
import os
import tempfile


def show(f, *args):
    try:
        print(repr(f(*args)))
    except BaseException as e:
        print(type(e).__name__, e)


print(mmap.PAGESIZE, mmap.ACCESS_READ, mmap.ACCESS_WRITE, mmap.ACCESS_COPY, mmap.PROT_READ | mmap.PROT_WRITE, mmap.MAP_SHARED)
m = mmap.mmap(-1, 16)
print(len(m), m.tell(), m.closed, m[0], m[:4], m[-1])
show(m.size)
m.write(b'hello')
print(m.tell(), m[:6], m.read(3), m.tell())
m.seek(0)
print(m.read(5), m.read_byte(), m.readline(), m.tell())
m.seek(2)
m.write_byte(0x58)
m[8:11] = b'abc'
m[15] = 65
print(m[:], m.find(b'abc'), m.rfind(b'\0'), m.find(b'zzz'), m.find(b'X', 1, 4))
m.move(0, 8, 3)
print(m[:12], bytes(m[::5]))
show(m.seek, 17)
show(m.seek, 3, 3)
show(m.write, b'x' * 20)
show(m.__getitem__, 16)
show(m.__setitem__, 0, 300)
show(m.__setitem__, slice(0, 2), b'abc')
show(m.resize, 32)
print(memoryview(m).nbytes, bytes(memoryview(m)[:5]), memoryview(m).readonly)
m.close()
print(m.closed, repr(m))
show(len, m)
show(m.read)
with tempfile.TemporaryFile() as f:
    f.write(b'0123456789' * 100)
    f.flush()
    with mmap.mmap(f.fileno(), 0) as mm:
        print(len(mm), mm.size(), mm[:12], mm.find(b'89', 8))
        mm[0:3] = b'abc'
        mm.flush()
        mm.resize(50)
        print(len(mm), mm.size(), mm[:5])
    f.seek(0)
    print(f.read(10), os.fstat(f.fileno()).st_size)
    ro = mmap.mmap(f.fileno(), 20, access=mmap.ACCESS_READ)
    show(ro.write, b'x')
    show(ro.__setitem__, 0, 1)
    print(ro[:5], memoryview(ro).readonly, repr(ro).split(',')[1])
    cow = mmap.mmap(f.fileno(), 20, access=mmap.ACCESS_COPY)
    cow[0] = 90
    f.seek(0)
    print(cow[:3], f.read(3))
    ro.close()
    cow.close()
    show(mmap.mmap, f.fileno(), 100000)
    show(mmap.mmap, f.fileno(), 10, mmap.MAP_SHARED, mmap.PROT_READ, mmap.ACCESS_READ, 1)
show(mmap.mmap, -1, 0)
show(lambda: mmap.mmap(-1, 10, access=7))
