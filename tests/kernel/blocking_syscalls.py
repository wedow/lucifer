# The blocking system calls, each waiting in the syscall bracket while another thread does what
# unblocks it: pipe and socket reads, select, poll, waitpid, flock, a timed receive, and the
# resolver. Every result is the oracle's.
import fcntl
import os
import select
import socket
import subprocess
import sys
import threading
import time

results = []


def in_thread(f, *args):
    box = []
    t = threading.Thread(target=lambda: box.append(f(*args)))
    t.start()
    return t, box


def after(delay, f, *args):
    t = threading.Timer(delay, f, args)
    t.start()
    return t


# A pipe read waits for the writer.
r, w = os.pipe()
t, box = in_thread(os.read, r, 10)
time.sleep(0.05)
os.write(w, b"hello")
t.join()
results.append(("pipe read", box))

# A full pipe blocks the writer until the reader drains it.
r, w = os.pipe()
os.set_blocking(w, False)
filled = 0
try:
    while True:
        filled += os.write(w, b"x" * 4096)
except BlockingIOError:
    pass
os.set_blocking(w, True)
t, box = in_thread(os.write, w, b"y" * 100)
time.sleep(0.05)
drained = 0
while drained < filled:
    drained += len(os.read(r, 65536))
t.join()
results.append(("pipe write", box, drained == filled))

# select and poll wake when the descriptor is ready, and time out when it is not.
r, w = os.pipe()
results.append(("select timeout", select.select([r], [], [], 0.05)))
t, box = in_thread(select.select, [r], [], [], 30)
time.sleep(0.05)
os.write(w, b"x")
t.join()
results.append(("select ready", [[x == r for x in group] for group in box[0]]))
os.read(r, 1)
p = select.poll()
p.register(r, select.POLLIN)
results.append(("poll timeout", p.poll(50)))
t, box = in_thread(p.poll, 30000)
time.sleep(0.05)
os.write(w, b"x")
t.join()
results.append(("poll ready", box))

# waitpid waits for the child and returns its status.
proc = subprocess.Popen([sys.executable, "-I", "-S", "-c", "import time; time.sleep(0.2); raise SystemExit(5)"])
t, box = in_thread(os.waitpid, proc.pid, 0)
t.join()
results.append(("waitpid", os.waitstatus_to_exitcode(box[0][1])))

# flock: a second descriptor of the same file waits for the first to unlock, or fails with LOCK_NB.
path = "/tmp/lucifer-blocking-%d" % os.getpid()
fd1 = os.open(path, os.O_CREAT | os.O_RDWR)
fd2 = os.open(path, os.O_RDWR)
fcntl.flock(fd1, fcntl.LOCK_EX)
try:
    fcntl.flock(fd2, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    results.append(("flock nonblocking", "refused"))
start = time.monotonic()
t, box = in_thread(fcntl.flock, fd2, fcntl.LOCK_EX)
time.sleep(0.1)
fcntl.flock(fd1, fcntl.LOCK_UN)
t.join()
results.append(("flock waited", box, time.monotonic() - start >= 0.1))
fcntl.lockf(fd1, fcntl.LOCK_UN)
os.close(fd1)
os.close(fd2)
os.unlink(path)

# A socket receive with a timeout raises socket.timeout; with data it returns it.
a, b = socket.socketpair()
a.settimeout(0.1)
try:
    a.recv(10)
except socket.timeout:
    results.append(("recv timeout", "timeout"))
t = after(0.05, b.send, b"data")
a.settimeout(30)
results.append(("recv", a.recv(10)))
t.join()
b.close()
results.append(("recv eof", a.recv(10)))

# The resolver, for literal addresses and the loopback name.
results.append(("numeric", socket.getaddrinfo("127.0.0.1", 80, 0, socket.SOCK_STREAM)))
results.append(("numeric6", socket.getaddrinfo("::1", 80, 0, socket.SOCK_STREAM, 0, socket.AI_NUMERICHOST)))
results.append(("localhost", sorted(socket.getaddrinfo("localhost", None, socket.AF_INET, socket.SOCK_STREAM))))
t, box = in_thread(socket.gethostbyname, "localhost")
t.join()
results.append(("gethostbyname", box))
try:
    socket.getaddrinfo("no-such-host.invalid", 80)
except socket.gaierror as e:
    results.append(("gaierror", type(e).__name__, e.errno in (socket.EAI_NONAME, socket.EAI_AGAIN)))

# Regular file I/O in threads.
path = "/tmp/lucifer-blocking-file-%d" % os.getpid()


def writer(n):
    with open(path + str(n), "wb") as f:
        for _ in range(100):
            f.write(b"z" * 1000)


ts = [threading.Thread(target=writer, args=(n,)) for n in range(4)]
for t in ts:
    t.start()
for t in ts:
    t.join()
results.append(("files", [os.path.getsize(path + str(n)) for n in range(4)]))
for n in range(4):
    os.unlink(path + str(n))

for r in results:
    print(*r)
