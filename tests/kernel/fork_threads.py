# os.fork with other threads running (FOUNDATIONS 2.9): four threads allocate and contend a Lock
# while the main thread forks twenty times. Each child allocates, starts and joins a thread and
# exits 0; the parent collects the statuses. The fork warning is recorded, not shown (it names the pid).
import os
import threading
import warnings

stop = threading.Event()
lock = threading.Lock()
count = [0]


def worker():
    junk = []
    while not stop.is_set():
        for _ in range(50):
            with lock:
                count[0] += 1
            junk.append([0] * 8)
        if len(junk) > 4000:
            junk.clear()


# A single-threaded process forks without the warning (before any thread has existed: an exited thread
# can linger in the process's OS thread count), and the child's exit is the parent's to see.
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    pid = os.fork()
    if pid == 0:
        os._exit(7)
single = (os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1]), [str(w.message) for w in caught])


workers = [threading.Thread(target=worker) for _ in range(4)]
for t in workers:
    t.start()

statuses = []
warned = 0
for i in range(20):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        pid = os.fork()
        if pid == 0:
            try:
                data = [str(n) for n in range(2000)]
                child = threading.Thread(target=lambda: data.append("x"))
                child.start()
                child.join()
                alone = threading.active_count() == 1 and threading.main_thread() is threading.current_thread()
                os._exit(0 if len(data) == 2001 and alone else 1)
            except BaseException:
                os._exit(2)
    warned += any(w.category is DeprecationWarning and "multi-threaded" in str(w.message) for w in caught)
    statuses.append(os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1]))

stop.set()
for t in workers:
    t.join()
print(statuses == [0] * 20, warned == 20, count[0] > 0)
print(*single)

# fork from a thread: the child's only thread is that one, and it becomes the main thread.
r, w = os.pipe()


def fork_in_thread():
    warnings.simplefilter("ignore")
    pid = os.fork()
    if pid == 0:
        main = threading.main_thread()
        os.write(w, ("child sees %d thread(s), main is current: %s" % (
            threading.active_count(), main is threading.current_thread())).encode())
        os._exit(0)
    os.waitpid(pid, 0)


t = threading.Thread(target=fork_in_thread)
t.start()
t.join()
os.close(w)
print(os.read(r, 100))
