import _io
import posix

NAME = "stdio_files.tmp"


def show(label, thunk):
    try:
        print(label, repr(thunk()))
    except BaseException as e:
        print(label, type(e).__name__, e)


# Text: write, read back, positions.
with open(NAME, "w", encoding="utf-8") as f:
    print(type(f).__name__, f.name, f.mode, f.encoding, f.errors, f.closed, f.newlines)
    print(f.write("one\ntwö\n"), f.tell(), f.writable(), f.readable(), f.seekable())
    f.write("three")
print(f.closed, repr(f))
show("closed write", lambda: f.write("x"))
show("closed read", lambda: f.read())
show("closed flush", lambda: f.flush())
show("closed tell", lambda: f.tell())
show("closed fileno", lambda: f.fileno())
f.close()
with open(NAME, encoding="utf-8") as f:
    print(f.read(2), f.tell(), f.readline(), repr(f.readline()), f.readline(), f.read(), f.newlines)
    print(f.seek(0), f.readlines(), f.seek(0), list(f), f.seek(4), f.read(3))
    show("seek cur", lambda: f.seek(1, 1))
    show("seek bad", lambda: f.seek(-1))
    show("write unreadable", lambda: f.write("x"))
with open(NAME, "rb") as f:
    print(type(f).__name__, f.read(5), f.peek(1)[:1], f.read1(2), f.tell(), f.raw.tell(), f.readinto(bytearray(3)))
    b = bytearray(10)
    print(f.seek(0), f.readinto(b), b, f.read(), f.read(1))
    print(f.raw.mode, f.raw.name, f.mode, f.readline(), f.fileno() == f.raw.fileno())
with open(NAME, "rb", buffering=0) as f:
    print(type(f).__name__, f.read(3), f.readall(), f.read(1), f.readinto(bytearray(2)))

# Newlines.
for newline in (None, "", "\n", "\r", "\r\n"):
    with open(NAME, "w", newline=newline, encoding="ascii") as f:
        f.write("a\nb\r\nc\rd")
    with open(NAME, "rb") as f:
        raw = f.read()
    with open(NAME, newline=newline, encoding="ascii") as f:
        print(repr(newline), raw, f.readlines(), f.newlines)
with open(NAME, "wb") as f:
    f.write(b"x\r\ny\rz\nw")
with open(NAME, newline=None) as f:
    print(f.read(), f.newlines)
with open(NAME, newline="") as f:
    print([f.readline() for _ in range(5)], f.newlines)

# Encodings and error handlers.
text = "café € \U0001f600 \ud800"
for encoding in ("utf-8", "latin-1", "ascii", "utf-16", "utf-16-le", "utf-32"):
    for errors in ("strict", "ignore", "replace", "backslashreplace", "xmlcharrefreplace", "surrogateescape",
                   "surrogatepass"):
        try:
            with open(NAME, "w", encoding=encoding, errors=errors) as f:
                f.write(text)
        except ValueError as e:
            print(encoding, errors, "write", type(e).__name__, e)
        with open(NAME, "rb") as f:
            raw = f.read()
        try:
            with open(NAME, encoding=encoding, errors=errors) as f:
                print(encoding, errors, raw, repr(f.read()))
        except ValueError as e:
            print(encoding, errors, raw, "read", type(e).__name__, e)
with open(NAME, "wb") as f:
    f.write(b"ab\xffcd\xe2\x82")
for errors in ("strict", "replace", "ignore", "backslashreplace", "surrogateescape"):
    show("bad bytes " + errors, lambda: open(NAME, encoding="utf-8", errors=errors).read())
with open(NAME, "w", encoding="utf-16") as f:
    f.write("a")
    f.write("b")
with open(NAME, "rb") as f:
    print(f.read())
with open(NAME, "a", encoding="utf-16") as f:
    f.write("c")
with open(NAME, "rb") as f:
    print(f.read())

# Modes and failures.
show("a mode", lambda: (open(NAME, "a").write("tail"), open(NAME).read()[-4:]))
show("x exists", lambda: open(NAME, "x"))
show("missing", lambda: open("stdio-no-such-file"))
show("directory", lambda: open("."))
show("bad mode", lambda: open(NAME, "z"))
show("bad mode2", lambda: open(NAME, "rw"))
show("bad mode3", lambda: open(NAME, "rbt"))
show("bin enc", lambda: open(NAME, "rb", encoding="utf-8"))
show("bin errors", lambda: open(NAME, "rb", errors="strict"))
show("bin newline", lambda: open(NAME, "rb", newline="\n"))
show("unbuffered text", lambda: open(NAME, "r", buffering=0))
show("bad encoding", lambda: open(NAME, encoding="no-such-codec"))
show("bad errors", lambda: open(NAME, encoding="utf-8", errors=3))
show("bad file", lambda: open(1.5))
show("bad newline", lambda: open(NAME, newline="x"))
show("closefd", lambda: open(NAME, closefd=False))
show("neg fd", lambda: open(-1))
show("opener", lambda: open(NAME, opener=lambda path, flags: posix.open(path, flags)).read()[:2])
show("bad opener", lambda: open(NAME, opener=lambda path, flags: -1))

# Read/write files.
with open(NAME, "w+") as f:
    f.write("hello world")
    print(f.seek(0), f.read(5), f.tell(), f.write("!"), f.seek(0), f.read())
    print(f.truncate(3), f.seek(0), f.read())
with open(NAME, "r+b") as f:
    print(type(f).__name__, f.read(2), f.write(b"ZZ"), f.seek(0), f.read(), f.truncate(1), f.tell())
with open(NAME, "w+b", buffering=3) as f:
    f.write(b"0123456789")
    f.seek(2)
    print(f.read(3), f.peek(), f.tell())
fd = posix.open(NAME, posix.O_RDONLY)
with open(fd, "rb") as f:
    print(f.fileno() == fd, f.read(), f.name == fd)
show("fd closed", lambda: posix.fstat(fd))
fd = posix.open(NAME, posix.O_RDONLY)
f = open(fd, "rb", closefd=False)
f.close()
print(posix.fstat(fd).st_size)
posix.close(fd)

# Memory streams.
b = _io.BytesIO(b"abc\ndef")
print(b.read(2), b.readline(), b.getvalue(), b.tell(), b.seek(1), b.write(b"ZZZZ"), b.getvalue(), b.truncate(2), b.getvalue())
s = _io.StringIO("l1\nl2\r\nl3")
print(s.readline(), s.readlines(), s.tell(), s.seek(0), s.read(4), s.getvalue(), s.newlines)
s = _io.StringIO(newline=None)
print(s.write("a\r\nb\n"), repr(s.getvalue()), s.seek(0), repr(s.read()))
s = _io.StringIO(newline="")
print(s.write("a\r\nb\n"), repr(s.getvalue()), s.seek(0), repr(s.read()))
show("stringio bytes", lambda: _io.StringIO().write(b"x"))
show("bytesio str", lambda: _io.BytesIO().write("x"))
b.close()
show("bytesio closed", lambda: b.read())
t = _io.TextIOWrapper(_io.BytesIO(b"caf\xc3\xa9\nx"), encoding="utf-8")
print(t.readline(), t.read(), t.tell(), t.seek(0), t.read(4), t.detach().tell())
show("detached", lambda: t.read())
print(_io.DEFAULT_BUFFER_SIZE, _io.UnsupportedOperation.__mro__[1:], _io.UnsupportedOperation.__module__)
print(_io.TextIOWrapper.__mro__, _io.FileIO.__mro__, _io.BufferedReader.__mro__)
show("text_encoding", lambda: (_io.text_encoding(None), _io.text_encoding("x")))
show("open_code", lambda: _io.open_code(NAME).read(1))
show("open_code type", lambda: _io.open_code(3))
rw = _io.BufferedRWPair(_io.BytesIO(b"in"), _io.BytesIO())
print(rw.read(1), rw.write(b"out"), rw.readable(), rw.writable())
