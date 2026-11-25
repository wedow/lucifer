# A unit built while one thread runs stores without locks and keeps the
# task's unlocked word set while it runs; a direct entry of it that
# declines (here: no recursion room left, so the call raises
# RecursionError) must put the word back. Then a second thread starts and
# both threads run the same units: the switch to parallel must see no unit
# running, and every store must land.
import sys
import threading

box = {'n': 0}


def bump(k):
    box['n'] += 1
    if k:
        return bump(k - 1) + 1
    return 0


def deep(k):
    box['n'] += 1
    return deep(k + 1)


def main():
    sys.setrecursionlimit(400)
    for _ in range(300):
        bump(8)
    hits = 0
    for _ in range(50):
        try:
            deep(0)
        except RecursionError:
            hits += 1
    box['n'] = 0
    out = []

    def run():
        t = 0
        for _ in range(2000):
            t += bump(8)
        out.append(t)

    ts = [threading.Thread(target=run) for _ in range(2)]
    for t in ts:
        t.start()
    run()
    for t in ts:
        t.join()
    print(hits, out, box['n'] <= 3 * 2000 * 9)


main()
