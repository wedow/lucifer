# Per-task state must survive a change of carrier: the task ident, thread
# locals, the signal and pending-call reasons aimed at the main task, a
# park that sleeps in place, a collection (stop-the-world) and a weakref
# callback. Every line is the same with one carrier or two.
import _signal
import _thread
import gc
import time
import _weakref as weakref

ident = _thread.get_ident()
local = _thread._local()
local.value = 7
stable = True
for i in range(300):
    stable = stable and _thread.get_ident() == ident and local.value == 7
print("ident and local stable:", stable)

got = []
_signal.signal(_signal.SIGUSR1, lambda signum, frame: got.append(signum))
for i in range(20):
    _thread.interrupt_main(_signal.SIGUSR1)
    for j in range(50):
        pass
print("signals run in the main task:", len(got))
_signal.raise_signal(_signal.SIGUSR1)
print("raise_signal runs the handler:", len(got))

start = time.monotonic()
for i in range(5):
    time.sleep(0.01)
print("sleeps:", time.monotonic() - start >= 0.05)

class Node:
    pass

dead = []
nodes = [Node() for i in range(200)]
refs = [weakref.ref(n, lambda r: dead.append(1)) for n in nodes]
del nodes
print("weakref callbacks:", len(dead))
print("collect:", gc.collect() >= 0, gc.isenabled())

total = 0
def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)
print("fib:", fib(15))
