# A thread's first access to a threading.local subclass runs __init__ while other threads use it.
import threading


class Mine(threading.local):
    def __init__(self, start):
        self.value = start


bad = []
for rnd in range(300):
    mine = Mine(10)
    mine.value += 1
    seen = []

    def use(n):
        try:
            mine.value += n
            seen.append(mine.value)
        except AttributeError as e:
            bad.append(str(e))

    ts = [threading.Thread(target=use, args=(n,)) for n in range(3)]
    [t.start() for t in ts]
    [t.join() for t in ts]
    if sorted(seen) != [10, 11, 12] or mine.value != 11:
        bad.append((sorted(seen), mine.value))
print(bad)
