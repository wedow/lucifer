# One thread flips a function's __code__ between a two-parameter and a
# no-parameter code, with no synchronisation, while another calls it with
# two arguments in a loop: every call either runs one of the codes or
# raises TypeError, and nothing leaks into the frame past its size.
import threading

def two(a, b):
    x = a + b
    return x
def none():
    return -1
codes = (two.__code__, none.__code__)
f = lambda a, b: 0
f.__code__ = codes[0]
stop = threading.Event()

def flip():
    k = 0
    while not stop.is_set():
        f.__code__ = codes[k & 1]
        k += 1

N = 20000
ok = err = bad = 0
t = threading.Thread(target=flip)
t.start()
try:
    for i in range(N):
        try:
            r = f(i, 1)
        except TypeError:
            err += 1
            continue
        if r == i + 1:
            ok += 1
        else:
            bad += 1
finally:
    stop.set()
    t.join()
f.__code__ = codes[0]
print(ok + err == N, bad, f(2, 3))
