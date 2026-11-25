# Signals and asynchronous exceptions reaching hot calls:
# interrupt_main tripped inside a callee raises where CPython raises it; a
# signal sent from another thread while sorted() runs its key is handled once
# and sorted's result holds; an exception sent to a thread in recursion stops
# it. (A pending call during recursion: tests/capi/pending_calls_run.py.)
import _signal
import _thread
import ctypes
import threading
import time


def trip(i):
    if i == 150:
        _thread.interrupt_main()
    return i


def caller(n):
    s = 0
    for i in range(n):
        s += trip(i)
    return s


def where(e):
    tb, names = e.__traceback__, []
    while tb is not None:
        names.append(tb.tb_frame.f_code.co_name)
        tb = tb.tb_next
    return names


for _ in range(3):
    try:
        caller(300)
        print('not interrupted')
    except KeyboardInterrupt as e:
        print('KeyboardInterrupt', where(e))

got = []
_signal.signal(_signal.SIGUSR1, lambda signum, frame: got.append(signum))
DATA = list(range(2000, 0, -1))
calls = [0]
sent = threading.Event()


def key(x):
    calls[0] += 1
    if calls[0] == 100:
        threading.Thread(target=lambda: (_signal.pthread_kill(main, _signal.SIGUSR1), sent.set())).start()
        sent.wait()
    return x


main = threading.get_ident()
for _ in range(3):
    got.clear()
    calls[0] = 0
    sent.clear()
    ok = sorted(DATA, key=key) == sorted(DATA)
    while not got:              # the handler runs at the main thread's next poll
        time.sleep(0.001)
    print('signal during sorted', ok, got)

def down(n):
    return 0 if n == 0 else down(n - 1) + 1


class Stop(Exception):
    pass


ready = threading.Event()
result = []


def spin():
    try:
        ready.set()
        while True:
            down(50)
    except Stop as e:
        result.append(where(e)[0])


w = threading.Thread(target=spin)
w.start()
ready.wait()
n = ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_ulong(w.ident), ctypes.py_object(Stop))
w.join()
print('async exception', n, result)
