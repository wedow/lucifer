# A hot loop reads the items of a list that a second thread, started by the
# loop halfway through, keeps refilling with fresh items: the replaced items
# lose the list's reference while the loop may still hold one it read. An
# item the loop holds must not be freed before the loop lets it go (its
# finalizer has not run when the loop uses it), and every item is freed
# exactly once by the end.
import gc
import threading

N = 300000
START = 100000
LOG = []
FREED = set()
made = [0]


class Item:
    def __init__(self, k):
        self.k = k
        made[0] += 1

    def __del__(self):
        LOG.append(self.k)
        FREED.add(self.k)


def refill(xs, n, base):
    for j in range(n):
        xs[j & 31] = Item(base + j)


def hot(xs, n):
    bad = 0
    th = None
    for k in range(n):
        v = xs[k & 31]
        if v.k in FREED:                # freed while the loop holds it
            bad += 1
        if k == START:
            th = threading.Thread(target=refill, args=(xs, N, 10 ** 7))
            th.start()
    return bad, th


def main():
    xs = [Item(i) for i in range(32)]
    bad, th = hot(xs, N)
    th.join()
    print("bad", bad)
    xs.clear()
    gc.collect()
    print("freed once", len(LOG) == len(set(LOG)) == made[0])


main()
