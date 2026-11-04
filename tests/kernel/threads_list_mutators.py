"""List mutators racing each other and lock-free readers: bounds are read
under the list's critical section, so no mutator sees a size that another
thread has already changed (no SystemError, no lost or doubled items)."""
import sys
import threading

# A collection after every allocation (-X lucifer_stress) leaves time for far fewer rounds.
STRESS = 'lucifer_stress' in sys._xoptions
ROUNDS = 40 if STRESS else 3000
errors = []


def run(*bodies):
    threads = [threading.Thread(target=b) for b in bodies]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def guarded(fn):
    def body():
        try:
            fn()
        except (IndexError, ValueError):
            pass
        except BaseException as e:
            errors.append(type(e).__name__)
    return body


def looped(*steps):
    """A body that runs each step ROUNDS times, tolerating the errors a race may cause."""
    def body():
        for i in range(ROUNDS):
            for s in steps:
                guarded(lambda: s(i))()
    return body


# Every mutator against every other, on a list that grows and empties.
L = list(range(8))
run(
    looped(lambda i: L.extend(range(5)), lambda i: L.insert(-3, i), lambda i: L.insert(i, i)),
    looped(lambda i: L.pop(), lambda i: L.pop(-2), lambda i: L.pop(0)),
    looped(lambda i: L.remove(i % 8), lambda i: L.__delitem__(-1), lambda i: L.__delitem__(i % 4)),
    looped(lambda i: L.__delitem__(slice(i % 3, None)), lambda i: L.__delitem__(slice(None, None, 2)),
           lambda i: L.__delitem__(slice(-1, None, -1))),
    looped(lambda i: L.__setitem__(slice(1, 4), [i, i]), lambda i: L.__setitem__(slice(-2, None), []),
           lambda i: L.__setitem__(slice(None, None, 2), [0] * len(L[::2])), lambda i: L.__setitem__(-1, i)),
    looped(lambda i: L.sort(), lambda i: L.reverse(), lambda i: L.__imul__(2) if len(L) < 50 else L.clear()),
    looped(lambda i: [x for x in L], lambda i: i in L, lambda i: L.index(i), lambda i: L.count(i), lambda i: L[::-1]),
)
print('mutators', errors[:3], all(type(x) is int for x in L))

# Items are neither lost nor doubled: producers insert tokens, consumers take each exactly once.
PER = 100 if STRESS else 2000
L = []
taken = []
done = []


def producer(base):
    def body():
        for i in range(PER):
            if i % 2:
                L.insert(-1 if L else 0, base + i)
            else:
                L.extend((base + i,))
    return body


def consumer(how):
    def body():
        mine = []
        while True:
            try:
                mine.append(L.pop() if how == 0 else L.pop(0) if how == 1 else L.pop(-1))
            except IndexError:
                if len(done) == 2:
                    if not L:
                        break
        taken.extend(mine)
    return body


def producers():
    run(producer(0), producer(10000))
    done.extend((1, 2))


run(producers, consumer(0), consumer(1))
print('conserved', sorted(taken) == sorted(list(range(PER)) + [10000 + i for i in range(PER)]), len(L))

# Slice deletion from the end against growth: a stale length once made the range exceed the list.
L = []
stop = []


def grow():
    while not stop:
        L.extend(range(10))


def shrink():
    for _ in range(ROUNDS * 4):
        try:
            del L[len(L) // 2:]
            L[len(L) // 2:] = [1, 2]
            del L[-3]
        except IndexError:
            pass
        except BaseException as e:
            errors.append(type(e).__name__)
    stop.append(1)


run(grow, shrink)
print('slices', errors[:3])
