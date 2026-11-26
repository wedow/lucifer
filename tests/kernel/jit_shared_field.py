# Both loads have a fixed receiver class. The slot's value and the
# inline field's value belong to the thread that created the instances.
import sys
import threading


class Slotted:
    __slots__ = ('value',)


class Plain:
    pass


def read_fields(n, a, b):
    total = 0
    for i in range(n):
        total += len(a.value) + len(b.value)
    return total


def worker():
    results.append(read_fields(n, a, b))


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
a, b = Slotted(), Plain()
a.value = bytearray(b'ab')
b.value = bytearray(b'c')
results = []
t = threading.Thread(target=worker)
t.start()
t.join()
print(results)
