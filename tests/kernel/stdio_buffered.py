import _io
import posix

NAME = "stdio_buffered.tmp"


def show(label, thunk):
    try:
        print(label, repr(thunk()))
    except BaseException as e:
        print(label, type(e).__name__, e)


# When does a buffered writer reach the file?
for size in (1, 8, 100):
    for chunks in ((1, 1, 1), (5, 10, 20), (7, 7, 7, 7, 7), (150, 3, 150), (100, 1, 99, 100), (0, 8, 8, 9)):
        raw = _io.FileIO(NAME, "w")
        writer = _io.BufferedWriter(raw, size)
        sizes = []
        for n in chunks:
            writer.write(b"x" * n)
            sizes.append(posix.fstat(raw.fileno()).st_size)
        sizes.append(writer.tell())
        writer.flush()
        sizes.append(posix.fstat(raw.fileno()).st_size)
        writer.close()
        print(size, chunks, sizes)

# And the raw reads of a buffered reader.


class Raw(_io._RawIOBase):
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.log = []

    def readable(self):
        return True

    def readinto(self, b):
        n = min(len(b), len(self.data) - self.pos)
        self.log.append(len(b))
        b[:n] = self.data[self.pos:self.pos + n]
        self.pos += n
        return n


DATA = b"".join(b"line %d\n" % i for i in range(30))
for size in (8, 16, 64):
    raw = Raw(DATA)
    reader = _io.BufferedReader(raw, size)
    out = [reader.read(3), reader.read(10), reader.peek(2)[:2], reader.read1(5), reader.readline(),
           reader.read(40), reader.readline(), reader.read1(), reader.read(0), reader.peek()[:3], len(reader.read())]
    print(size, out, raw.log)
    raw = Raw(DATA)
    reader = _io.BufferedReader(raw, size)
    print(size, list(reader)[:3], raw.log, reader.tell(), reader.raw.pos)

# BufferedRandom: reads and writes through one buffer.
with open(NAME, "wb") as f:
    f.write(bytes(range(256)) * 4)
for size in (16, 1000):
    f = _io.BufferedRandom(_io.FileIO(NAME, "r+"), size)
    print(size, f.read(5), f.write(b"AB"), f.tell(), f.read(3), f.seek(2), f.write(b"CD"), f.read(2),
          f.peek(1)[:1], f.seek(0, 2), f.write(b"END"), f.seek(-3, 2), f.read(), f.truncate(10), f.seek(0), f.read())
    f.close()
show("peek write-only", lambda: _io.BufferedWriter(_io.FileIO(NAME, "w")).peek())
show("read write-only", lambda: _io.BufferedWriter(_io.FileIO(NAME, "w")).read())
show("write read-only", lambda: _io.BufferedReader(_io.FileIO(NAME)).write(b"x"))
show("bad buffer size", lambda: _io.BufferedReader(_io.FileIO(NAME), 0))
show("reader of writer", lambda: _io.BufferedReader(_io.FileIO(NAME, "w")))
show("writer of reader", lambda: _io.BufferedWriter(_io.FileIO(NAME)))
show("rwpair", lambda: (lambda p: (p.read(2), p.write(b"zz"), p.closed))(_io.BufferedRWPair(_io.BytesIO(b"abc"), _io.BytesIO())))
r = _io.BufferedReader(_io.FileIO(NAME))
r.close()
for name in ("read", "peek", "readline", "tell", "seek", "flush", "readinto", "read1", "truncate"):
    args = {"readinto": (bytearray(1),), "seek": (0,)}.get(name, ())
    show("closed " + name, lambda: getattr(r, name)(*args))
w = _io.BufferedWriter(_io.FileIO(NAME, "w"))
w.close()
for name in ("write", "flush", "tell", "seek", "truncate"):
    args = {"write": (b"x",), "seek": (0,)}.get(name, ())
    show("closed w " + name, lambda: getattr(w, name)(*args))
print(w.closed, w.raw.closed, repr(w), repr(r.raw))
w = _io.BufferedWriter(_io.FileIO(NAME, "w"))
print(w.detach().closed, repr(w))
show("detached", lambda: w.write(b"x"))
show("detached closed", lambda: w.closed)
