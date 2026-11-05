# PEP 475: a blocking call interrupted by a signal runs the Python handler, then resumes for the
# time that is left; a handler that raises ends the call with its exception. The timer signal
# arrives while the call waits.
import os
import select
import signal
import socket
import time

hits = []


def returns(signum, frame):
    hits.append(signum)


class Boom(Exception):
    pass


def raises(signum, frame):
    raise Boom


def timed(call, delay=0.05, repeat=0.0):
    """Runs call() with SIGALRM due after delay; the elapsed time in tenths of a second, floored."""
    hits.clear()
    signal.setitimer(signal.ITIMER_REAL, delay, repeat)
    t0 = time.monotonic()
    try:
        result = call()
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
    return result, time.monotonic() - t0


r, w = os.pipe()
sock_a, sock_b = socket.socketpair()
child_pid = None


def sleep_call():
    time.sleep(0.3)


def select_call():
    return select.select([r], [], [], 0.3)


def poll_call():
    p = select.poll()
    p.register(r, select.POLLIN)
    return p.poll(300)


def read_ready():
    # the writer arrives after the signal; the read must not fail with EINTR
    pid = os.fork()
    if pid == 0:
        time.sleep(0.2)
        os.write(w, b"x")
        os._exit(0)
    data = os.read(r, 1)
    os.waitpid(pid, 0)
    return data


def write_call():
    # a full pipe: the write blocks until the reader drains it after the signal
    r2, w2 = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(w2)
        time.sleep(0.2)
        while os.read(r2, 65536):
            pass
        os._exit(0)
    try:
        return os.write(w2, bytes(1 << 20)) > 0
    finally:
        os.close(w2)
        os.close(r2)
        os.waitpid(pid, 0)


def waitpid_call():
    pid = os.fork()
    if pid == 0:
        time.sleep(0.2)
        os._exit(3)
    got = os.waitpid(pid, 0)
    return os.waitstatus_to_exitcode(got[1])


def recv_call():
    sock_a.settimeout(0.3)
    try:
        sock_a.recv(1)
    except TimeoutError:
        return "timeout"


def lock_call():
    import threading
    lock = threading.Lock()
    lock.acquire()
    return lock.acquire(timeout=0.3)


calls = [sleep_call, select_call, poll_call, read_ready, write_call, waitpid_call, recv_call, lock_call]

signal.signal(signal.SIGALRM, returns)
for call in calls:
    result, elapsed = timed(call)
    print(call.__name__, "returns", result, hits == [signal.SIGALRM], elapsed >= (0.2 if call in (read_ready, write_call, waitpid_call) else 0.3))

# a repeating signal keeps interrupting; the deadline is not stretched
for call in (sleep_call, select_call, poll_call):
    result, elapsed = timed(call, 0.02, 0.02)
    print(call.__name__, "repeating", result, len(hits) >= 2, 0.3 <= elapsed < 2.0)

signal.signal(signal.SIGALRM, raises)
for call in calls:
    try:
        timed(call, 0.05)
    except Boom:
        print(call.__name__, "raises")
