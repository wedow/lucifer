# A pinned task never changes carrier: code that C calls into (a signal
# handler, an atexit callback) sees one native thread id however often it
# polls, while the main task around it moves under -X lucifer_handoff.
import _signal
import _thread
import atexit


def carriers_seen():
    seen = set()
    for i in range(1000):
        seen.add(_thread.get_native_id())
    return len(seen)


def handler(signum, frame):
    print("signal handler saw", carriers_seen())


def at_exit():
    print("atexit callback saw", carriers_seen())


_signal.signal(_signal.SIGUSR1, handler)
atexit.register(at_exit)
_signal.raise_signal(_signal.SIGUSR1)
print("main saw", carriers_seen())
