# A Python signal handler and an interrupt_main async exception fire while another thread sorts a
# large list. The handler and the KeyboardInterrupt run in the main thread; the sort is unharmed.
# The sorting thread's key function sends the signals; the main thread waits in join, so the
# handler runs when the wait is interrupted or right after it. The results are printed after join.
# Each signal that raises waits for the main thread to release the in_try lock inside its try, so the
# exception always lands in the try. The gate is a bare lock, not an Event: an exception landing inside
# Event.set could leave its condition locked. The try body keeps running safepoints until it lands.
import _thread
import os
import signal
import threading
import time

N = 20000
data = [(i * 7919) % N for i in range(N)]
handled = []
calls = [0]
result = []
in_try = _thread.allocate_lock()
in_try.acquire()


limit = time.monotonic() + 60    # a bound that fails the output, not a synchronisation


def handler(signum, frame):
    handled.append((signum == signal.SIGUSR1, threading.current_thread() is threading.main_thread()))


signal.signal(signal.SIGUSR1, handler)


def key(x):
    calls[0] += 1
    if calls[0] == N // 3:
        os.kill(os.getpid(), signal.SIGUSR1)
    elif calls[0] == N // 2:
        in_try.acquire()
        _thread.interrupt_main()
    return x


def sorter():
    result.append(sorted(data, key=key))


t = threading.Thread(target=sorter)
t.start()
interrupted = 0
try:
    in_try.release()
    t.join()
    # An interrupt raised while main waits in join, or any time after it, is delivered at a safepoint of
    # this loop: the try is left only by the KeyboardInterrupt (or the deadline, which fails the output).
    while time.monotonic() < limit:
        pass
except KeyboardInterrupt:
    interrupted += 1
t.join()
while not handled and time.monotonic() < limit:
    pass
print(result[0] == sorted(data), calls[0], handled, interrupted)

# A signal sent while the main thread sorts, handled by it, with another thread alive.
stop = threading.Event()
bystander = threading.Thread(target=stop.wait)
bystander.start()
handled.clear()
calls[0] = 0


def key_main(x):
    calls[0] += 1
    if calls[0] == N // 2:
        signal.raise_signal(signal.SIGUSR1)
    return x


print(sorted(data, key=key_main) == sorted(data), handled)
stop.set()
bystander.join()

# An exception raised by a handler during another thread's sort ends the main thread's wait only.
signal.signal(signal.SIGUSR1, lambda s, f: 1 / 0)
calls[0] = 0
result.clear()


def key_kill(x):
    calls[0] += 1
    if calls[0] == N // 2:
        in_try.acquire()
        os.kill(os.getpid(), signal.SIGUSR1)
    return x


t = threading.Thread(target=lambda: result.append(sorted(data, key=key_kill)))
t.start()
caught = 0
try:
    in_try.release()
    t.join()
    while time.monotonic() < limit:
        pass
except ZeroDivisionError:
    caught += 1
t.join()
print(caught, result[0] == sorted(data))
