import posix
import sys

# The three standard streams as startup configures them.
for stream in (sys.stdin, sys.stdout, sys.stderr):
    print(type(stream).__module__, type(stream).__name__, stream.name, stream.mode, stream.encoding,
          stream.errors, stream.line_buffering, stream.write_through, stream.closed,
          type(stream.buffer).__name__, type(getattr(stream.buffer, "raw", stream.buffer)).__name__,
          getattr(stream.buffer, "raw", stream.buffer).closefd)
print(sys.__stdin__ is sys.stdin, sys.__stdout__ is sys.stdout, sys.__stderr__ is sys.stderr)
print([s.fileno() for s in (sys.stdin, sys.stdout, sys.stderr)], sys.stdout.isatty(), sys.stdout.writable())
print(repr(sys.stdout), repr(getattr(sys.stderr.buffer, "raw", sys.stderr.buffer)))
print(sys.stdout.readable(), sys.stdin.readable(), sys.stdin.writable(), sys.stdout.seekable() in (True, False))

# write returns the number of characters; print goes through sys.stdout.
print(sys.stdout.write("héllo € \U0001f600\n"))
print("sep", 1, 2.5, None, sep="|", end="!\n")
print()
print("é", "中文", b"bytes", [1, "é"])
print(sys.stdout.write(""), sys.stdout.writelines(["a\n", "b\n"]))
print("to stderr", file=sys.stderr)
print("flushed", flush=True)
try:
    sys.stdout.write(b"bytes")
except TypeError as e:
    print(e)
try:
    print("\ud800")
except UnicodeEncodeError as e:
    print(e)
print("\ud800", file=sys.stderr)
sys.stderr.write("café €\n")

# Replacing sys.stdout; print(file=None) follows it.
class Sink:
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append(text)
        return len(text)


real = sys.stdout
sys.stdout = sink = Sink()
print("a", "b", end="")
print()
sys.stdout = real
print(sink.parts)
sys.stdout = None
print("nothing")
sys.stdout = real
del sys.stdout
try:
    print("x")
except RuntimeError as e:
    real.write(str(e) + "\n")
sys.stdout = real

# One pipe for both streams: stdout is block buffered, stderr is line buffered
# and direct writes jump the queue.
posix.dup2(1, 2)
print("out 1")
print("err 1", file=sys.stderr)
sys.stderr.write("partial ")
posix.write(1, b"[direct 1]\n")
sys.stderr.write("line\n")
posix.write(2, b"[direct 2]\n")
print("out 2")
sys.stdout.flush()
print("err 2", file=sys.stderr, flush=True)
print("out 3", end="")
sys.stdout.write("\n")
sys.stderr.write("no newline")
posix.write(1, b"[direct 3]\n")
sys.stderr.flush()
big = "x" * 5000
for i in range(3):
    print(big, i)
posix.write(2, b"[direct 4]\n")
sys.stdout.write(big * 40 + "\n")
posix.write(2, b"[direct 5]\n")
print("out 4")
print("last words")
print("stderr last", file=sys.stderr)
sys.stderr.write("tail")
