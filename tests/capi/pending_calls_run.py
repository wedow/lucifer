# Py_AddPendingCall from another thread (through _testcapi) while the main
# thread runs loop-free recursion hot: the call runs, once, as on CPython.
import threading
import _testcapi

ran = []


def down(n):
    return 0 if n == 0 else down(n - 1) + 1


def recurse_until(flag):
    while not flag:
        down(50)


def pending():
    ran.append(1)


for _ in range(300):
    down(50)
started = threading.Event()


def add():
    started.wait()
    _testcapi._pending_threadfunc(pending, num=1, blocking=True, ensure_added=True)


t = threading.Thread(target=add)
t.start()
started.set()
recurse_until(ran)
t.join()
print('pending call ran', ran)
