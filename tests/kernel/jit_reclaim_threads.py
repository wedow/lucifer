# Dead units are freed while other threads run compiled code: workers
# call functions (some from C, so that a poll stops them inside the
# caller's unit) and resume generators whose units rely on a global, a
# writer keeps rebinding it (each rebinding kills the units), and a
# collector thread keeps collecting, which frees the dead units no
# activation is in. Every worker's sum is the same whichever function
# the global names.
import gc
import threading


def one():
    return 1


def uno():
    return 1


F = one
ROUNDS = 3000


def work(i):
    t = F()
    for j in range(i & 7):
        t += j & 1
    return t


def deep(k):
    return F() if k == 0 else deep(k - 1) + F()


def steps(n):
    for i in range(n):
        yield F() + i


def worker(out, k):
    total = 0
    for i in range(ROUNDS):
        total += sum(map(work, range(i & 15))) + deep(i % 7)
        if i % 50 == 0:
            g = steps(4)
            total += next(g)
            total += sum(g)
    out[k] = total


def writer(stop):
    global F
    while not stop.is_set():
        F = uno if F is one else one


def collector(stop):
    while not stop.is_set():
        gc.collect()


def main():
    out = [None] * 4
    stop = threading.Event()
    side = [threading.Thread(target=writer, args=(stop,)), threading.Thread(target=collector, args=(stop,))]
    workers = [threading.Thread(target=worker, args=(out, k)) for k in range(4)]
    for t in side + workers:
        t.start()
    for t in workers:
        t.join()
    stop.set()
    for t in side:
        t.join()
    print(out)


main()
