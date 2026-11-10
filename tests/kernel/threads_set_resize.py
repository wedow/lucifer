"""Threads resize one set while others add to it or clear it: a bulk update
from a set or a dict sizes the table for the keys it brings, and a
difference_update shrinks the table once it holds mostly dummies. The size is
chosen from the set as it is when the table is rebuilt, so no key is lost and
the rebuild never runs out of room."""
import threading

THREADS = 4
ROUNDS = 150
src = set(range(1000, 1060))
src_dict = dict.fromkeys(range(2000, 2060))


def run(fns):
    bar = threading.Barrier(len(fns))

    def body(fn):
        bar.wait()
        fn()

    ts = [threading.Thread(target=body, args=(fn,)) for fn in fns]
    for t in ts:
        t.start()
    for t in ts:
        t.join()


def adder(s, k):
    def add():
        for i in range(k * 100, k * 100 + 100):
            s.add(i)
    return add


# A set and a dict merged in while two threads add.
off = 0
for _ in range(ROUNDS):
    s = set()
    run([lambda: s.update(src), lambda: s.update(src_dict), adder(s, 1), adder(s, 2)])
    off += s != src | src_dict.keys() | set(range(100, 300))
print('update', off)

# A set merged into one that another thread clears meanwhile.
wrong = 0
for _ in range(ROUNDS):
    s = set(range(5))
    run([lambda: s.update(src), s.clear])
    wrong += not (s <= src | set(range(5)))
print('clear', wrong)

# Most keys removed, which shrinks the table, while two threads add.
off = 0
for _ in range(ROUNDS):
    s = set(range(3000, 3400))
    gone = set(range(3000, 3390))
    run([lambda: s.difference_update(gone), adder(s, 1), adder(s, 2)])
    off += s != set(range(3390, 3400)) | set(range(100, 300))
print('shrink', off)
