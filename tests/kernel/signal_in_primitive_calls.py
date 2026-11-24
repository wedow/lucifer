"""A signal that arrives while a loop makes builtin calls, which run the
kernel's primitives, runs its handler at the next poll: each primitive call
from the eval loop polls after it returns, and so do RESUME and back-edges.
The handler then stops the loop."""
import signal

hits = []
signal.signal(signal.SIGALRM, lambda signum, frame: hits.append(1))
for round in range(5):
    hits.clear()
    signal.setitimer(signal.ITIMER_REAL, 0.002)
    n = 0
    while not hits:
        isinstance(n, str)
        len('abc')
        n += 1
    print(round, len(hits))
