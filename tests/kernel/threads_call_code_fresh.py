# One thread installs a fresh copy (code.replace()) of a two-parameter or a
# no-parameter code as a function's __code__, with no synchronisation, while
# another calls the function with two arguments from a hot loop: the code
# replaced has no reference but the function's, so it dies at the store.
# Every call either runs one of the codes or raises TypeError.
import threading

def two(a, b):
    x = a + b
    return x
def none():
    return -1
codes = (two.__code__, none.__code__)
f = lambda a, b: 0
f.__code__ = codes[0].replace()
stop = threading.Event()
flips = [0]

def flip():
    k = 0
    while not stop.is_set():
        f.__code__ = codes[k & 1].replace()
        k += 1
        for _ in range(50):     # each copy is called a few times, so it gets units of its own
            pass
    flips[0] = k

def calls(n):
    ok = err = bad = 0
    for i in range(n):
        try:
            r = f(i, 1)
        except TypeError:
            err += 1
            continue
        if r == i + 1 or r == -1:
            ok += 1
        else:
            bad += 1
    return ok, err, bad

N = 50000
t = threading.Thread(target=flip)
t.start()
try:
    ok, err, bad = calls(N)
finally:
    stop.set()
    t.join()
f.__code__ = codes[0]
print(ok + err == N, bad, flips[0] > 0, f(2, 3))
