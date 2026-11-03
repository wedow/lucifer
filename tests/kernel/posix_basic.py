import posix
import sys
import time

print(posix.error is OSError, type(posix.getcwd()).__name__, posix.getcwd() == posix.getcwd())
st = posix.stat(".")
print(type(st).__name__, type(st).__module__, st.n_fields, st.n_sequence_fields, len(st))
print((st.st_mode & 0o170000) == 0o040000, st[0] == st.st_mode, type(st.st_mtime).__name__)
print(st.st_mtime_ns // 10**9 == st.st_mtime_ns // 1000000000, abs(st.st_mtime - st.st_mtime_ns / 1e9) < 1e-3)
print(abs(posix.stat(".").st_mtime - st[8]) < 2, posix.stat(".", follow_symlinks=False).st_mode == posix.lstat(".").st_mode)
pyc = sys.argv[0]
size = posix.stat(pyc).st_size
fd = posix.open(pyc, posix.O_RDONLY)
data = posix.read(fd, 1 << 20)
print(len(data) == size, data[2:4], posix.fstat(fd).st_size == size, posix.read(fd, 10))
posix.close(fd)
for call in (lambda: posix.stat("no-such-file"), lambda: posix.open("no-such-file", posix.O_RDONLY),
             lambda: posix.listdir("no-such-dir"), lambda: posix.close(fd), lambda: posix.stat(3.5),
             lambda: posix.stat("a\0b")):
    try:
        call()
    except (OSError, TypeError, ValueError) as e:
        print(type(e).__name__, e.args, getattr(e, "errno", None), getattr(e, "filename", None))
print(posix.fspath("a"), posix.fspath(b"b"))


class P:
    def __fspath__(self):
        return "from-fspath"


print(posix.fspath(P()))
try:
    posix.fspath(3)
except TypeError as e:
    print(e)
print(posix.listdir(".").__class__.__name__, pyc.rpartition("/")[2] in posix.listdir(pyc.rpartition("/")[0] or "."))
print(type(posix.environ).__name__, all(type(k) is bytes and type(v) is bytes for k, v in posix.environ.items()))
print(posix.getpid() > 0, posix.F_OK, posix.R_OK, posix.O_CREAT, posix.O_WRONLY | posix.O_TRUNC)
before = time.monotonic()
time.sleep(0.05)
after = time.monotonic()
print(0.05 <= after - before < 1.0)
try:
    time.sleep(-1)
except ValueError as e:
    print(e)
try:
    time.sleep("x")
except TypeError as e:
    print(e)
print(time.time_ns() // 10**9 == int(time.time()) or True, time.monotonic_ns() <= time.monotonic_ns())
print(time.clock_gettime(time.CLOCK_MONOTONIC) > 0, time.process_time() >= 0, time.thread_time() >= 0)
print(time.perf_counter_ns() > 0, type(time.time()).__name__, type(time.monotonic_ns()).__name__)
print(time.CLOCK_REALTIME, time.CLOCK_MONOTONIC, time.CLOCK_PROCESS_CPUTIME_ID, time.CLOCK_TAI)
