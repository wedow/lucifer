"""Eight threads make and drop short-lived classes on one base while others
list its subclasses: the base's subclass list is changed by every class
creation and every class death."""
import threading


class Base:
    pass


def make(n):
    total = 0
    for r in range(n):
        total += type('K', (Base,), {'x': r}).x
    return total


def read(n):
    seen = 0
    for _ in range(n):
        for c in Base.__subclasses__():
            seen += c is not None
    return seen


made = []


def worker(f, n):
    r = f(n)
    if f is make:
        made.append(r)


threads = [threading.Thread(target=worker, args=(make, 400)) for _ in range(8)]
threads += [threading.Thread(target=worker, args=(read, 200)) for _ in range(2)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(made == [sum(range(400))] * 8)
