"""Bulk reads of a set or dict that another thread changes: set.update from a
set and from a dict, set union, dict.update and dict.fromkeys, into targets
whose keys need no __eq__ and into targets whose keys only __eq__ can judge.
Each takes the source whole, as CPython's merges do under both objects'
locks: a key is never paired with another key's hash, and the keys the
source held throughout are all copied, however the other thread reuses
entries, resizes the table or pops items."""
import threading

ROUNDS = 40
STABLE = range(200)
CHURN = range(1000, 1300)


class K:
    """Equal by value: a key that only __eq__ can judge."""

    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return hash(self.v)

    def __eq__(self, other):
        return type(other) is K and other.v == self.v


EQ_KEYS = range(20)


def churn(container, stop):
    """Adds keys until the table grows, then removes them, which leaves
    entries for the next adds to reuse. A dict's removals alternate between
    pop and popitem, which hands its entry numbers out again."""
    if type(container) is dict:
        def body():
            rounds = 0
            while not stop:
                for k in CHURN:
                    container.setdefault(k)
                for k in CHURN:
                    if rounds % 2:
                        container.popitem()
                    else:
                        container.pop(k)
                rounds += 1
        return body

    def body():
        while not stop:
            for k in CHURN:
                container.add(k)
            for k in CHURN:
                container.discard(k)
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
            if any(type(k) is K for k in got):
                torn += sum(type(k) is K for k in got) != len(EQ_KEYS)
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


def into_eq_set(source):
    s = {K(i) for i in EQ_KEYS}
    s.update(source)
    return s


def into_eq_dict(source):
    d = {K(i): 'target' for i in EQ_KEYS}
    d.update(source)
    if any(d[K(i)] != 'source' for i in EQ_KEYS):
        raise TypeError('a value was not replaced')
    return d


def with_eq_keys(source):
    for i in EQ_KEYS:
        source[K(i)] = 'source'
    return source


check('set.update(set)', into_set, set(STABLE))
check('set.update(dict)', into_set, dict.fromkeys(STABLE))
check('set | set', lambda source: {-1} | source, set(STABLE))
check('dict.update(dict)', into_dict, dict.fromkeys(STABLE))
check('dict.fromkeys(dict)', dict.fromkeys, dict.fromkeys(STABLE))
check('dict.fromkeys(set)', dict.fromkeys, set(STABLE))
check('eq set.update(set)', into_eq_set, set(STABLE) | {K(i) for i in EQ_KEYS})
check('eq set.update(dict)', into_eq_set, with_eq_keys(dict.fromkeys(STABLE)))
check('eq dict.update(dict)', into_eq_dict, with_eq_keys(dict.fromkeys(STABLE)))
