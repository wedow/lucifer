"""Bulk reads of a set or dict that another thread changes: set.update from a
set and from a dict, set union, dict.update and dict.fromkeys. Each takes the source
whole, as CPython's merges do under both objects' locks: a key is never
paired with another key's hash, and the keys the source held throughout are
all copied, however the other thread reuses entries or resizes the table."""
import threading

ROUNDS = 40
STABLE = range(200)
CHURN = range(1000, 1300)


def churn(container, stop):
    """Adds keys until the table grows, then removes them, which leaves
    entries for the next adds to reuse."""
    add, remove = (container.setdefault, container.pop) if type(container) is dict \
        else (container.add, container.discard)

    def body():
        while not stop:
            for k in CHURN:
                add(k)
            for k in CHURN:
                remove(k)
    return body


def check(name, take, source):
    stop = []
    t = threading.Thread(target=churn(source, stop))
    t.start()
    lost = torn = errors = 0
    try:
        for _ in range(ROUNDS):
            try:
                got = take(source)
            except (TypeError, RuntimeError):
                errors += 1
                continue
            lost += sum(k not in got for k in STABLE)
            torn += sum(k not in got for k in list(got))
    finally:
        stop.append(True)
        t.join()
    print(name, lost, torn, errors)


def into_set(source):
    s = {-1}
    s.update(source)
    return s


def into_dict(source):
    d = {-1: -1}
    d.update(source)
    return d


check('set.update(set)', into_set, set(STABLE))
check('set.update(dict)', into_set, dict.fromkeys(STABLE))
check('set | set', lambda source: {-1} | source, set(STABLE))
check('dict.update(dict)', into_dict, dict.fromkeys(STABLE))
check('dict.fromkeys(dict)', dict.fromkeys, dict.fromkeys(STABLE))
check('dict.fromkeys(set)', dict.fromkeys, set(STABLE))
