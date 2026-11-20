"""Two threads write to one text file while a third iterates it (CPython's
test_io test_write_readline_races, gh-134908). A TextIOWrapper's methods are
one critical section each, as Modules/_io/textio.c's are: a reader's flush
of the pending writes never drops a write appended meanwhile (luc-rqz6)."""
import os
import tempfile
import threading

WRITES = 400


def writer(f, barrier):
    barrier.wait()
    for _ in range(WRITES):
        f.write("x")


def reader(f, barrier, bad):
    barrier.wait()
    for _ in range(WRITES):
        for line in f:
            if line != "":
                bad.append(line)


with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, "text")
    bad = []
    for r in range(5):
        with open(path, "w+", encoding="utf-8") as f:
            barrier = threading.Barrier(3)
            threads = [threading.Thread(target=writer, args=(f, barrier)) for _ in range(2)]
            threads.append(threading.Thread(target=reader, args=(f, barrier, bad)))
            for t in threads:
                t.start()
            for t in threads:
                t.join()
        print(r, os.stat(path).st_size, bad[:1])
