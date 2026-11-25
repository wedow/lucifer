# Hot `yield from` and `await` on several threads at once, each delegating
# frame's SEND compiled: every thread walks its own trees of delegating
# generators and drives its own await chains, and two threads share one
# tree of tuples (only read).
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 200


def tree(lo, hi):
    if lo >= hi:
        return None
    mid = (lo + hi) // 2
    return (tree(lo, mid), mid, tree(mid + 1, hi))


def walk(t):
    if t is None:
        return 0
    left, value, right = t
    n = yield from walk(left)
    yield value
    m = yield from walk(right)
    return n + m + 1


class Step:
    def __await__(self):
        got = yield 'step'
        return got


async def chain(depth):
    if depth == 0:
        return await Step()
    return await chain(depth - 1) + depth


def worker(k, shared, results):
    own = tree(0, 20 + k)
    total = count = 0
    for i in range(N):
        g = walk(shared if i % 2 else own)
        while True:
            try:
                total += next(g)
            except StopIteration as e:
                count += e.value
                break
        c = chain(5)
        c.send(None)
        try:
            c.send(i)
        except StopIteration as e:
            total += e.value
    results[k] = (total, count)


shared = tree(0, 33)
results = [None] * 4
threads = [threading.Thread(target=worker, args=(k, shared, results)) for k in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(results)
