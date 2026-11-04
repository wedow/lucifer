# pthread_sigmask alone pins: it is the first thread-scoped call this program makes.
import _thread
import signal

old = signal.pthread_sigmask(signal.SIG_BLOCK, [signal.SIGUSR2])
print(signal.SIGUSR2 in signal.pthread_sigmask(signal.SIG_BLOCK, []), old == set())
seen = set()
for i in range(2000):
    seen.add(_thread.get_native_id())
print(len(seen))
signal.pthread_sigmask(signal.SIG_SETMASK, old)
