# List and tuple items read in hot loops, where compiled code may hold an
# item without counting it while nothing between its read and its last use
# can free it. Each shape here frees the item's other holder before the
# last use: the list cleared, the slot overwritten, the list deleted, the
# tuple replaced, through a call, a finalizer run by an unrelated release,
# __eq__, __hash__ and __bool__ callbacks, an exception handler and a
# generator's suspension, and a borrowed item of a borrowed row of a grid
# whose row is replaced. The item must still be alive at its use, as the
# local that names it keeps it in CPython, and be freed exactly when that
# local lets it go: each use records whether the item's finalizer has run
# (it must not have), and the log shows what was freed by the round's end.
import gc

LOG = []
ROUNDS = 300


class Item:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        LOG.append(self.k)


def fresh(tag, n=4):
    return [Item(f"{tag}{i}") for i in range(n)]


def check(name, f, *args):
    LOG.clear()
    for r in range(ROUNDS):
        got = f(*args, r)
        if r == 0 or r == ROUNDS - 1:
            print(name, r, got, sorted(LOG))
        LOG.clear()
    gc.collect()


def via_call(r):
    xs = fresh("c")
    seen = []
    for v in xs:
        xs.clear()                      # a call: the list lets go of every item
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet                # v is still alive
    return seen


def via_store(r):
    xs = fresh("s")
    seen = []
    for i in range(len(xs)):
        v = xs[i]
        xs[i] = None                    # the slot overwritten: the list's reference goes
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


def via_del(r):
    xs = fresh("d")
    seen = []
    it = iter(xs)
    for v in it:
        del xs                          # the local goes; the iterator keeps the list
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
        xs = None
    return seen


class Trigger:
    def __init__(self, target):
        self.target = target

    def __del__(self):
        self.target.clear()


def via_finalizer(r):
    xs = fresh("f")
    seen = []
    for v in xs:
        t = Trigger(xs)
        del t                           # an unrelated release whose finalizer clears the list
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


class Clearer:
    def __init__(self, target):
        self.target = target

    def __eq__(self, other):
        self.target.clear()
        return False

    def __hash__(self):
        self.target.clear()
        return 1

    def __bool__(self):
        self.target.clear()
        return True


def via_eq(r):
    xs = fresh("e")
    c = Clearer(xs)
    seen = []
    for v in xs:
        if c == v:
            pass
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


def via_hash(r):
    xs = fresh("h")
    c = Clearer(xs)
    d = {}
    seen = []
    for v in xs:
        d[c] = 1
        seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


def via_bool(r):
    xs = fresh("b")
    c = Clearer(xs)
    seen = []
    for v in xs:
        if c:
            seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


def boom(xs):
    xs.clear()
    raise ValueError


def via_handler(r):
    xs = fresh("x")
    seen = []
    for v in xs:
        try:
            boom(xs)
        except ValueError:
            seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
    return seen


def items(xs):
    for v in xs:
        yield v.k
        yield (v.k, v.k in LOG)         # after the suspension, in which the list was cleared


def via_generator(r):
    xs = fresh("g")
    seen = []
    g = items(xs)
    for k in g:
        xs.clear()
        seen.append(k)
    return seen


def via_tuple(r):
    ps = [(Item(f"t{i}a"), Item(f"t{i}b")) for i in range(3)]
    seen = []
    for i in range(len(ps)):
        a, b = ps[i]
        ps[i] = None                    # the tuple goes, and with it its references to a and b
        seen.append((a.k + b.k, a.k in LOG or b.k in LOG))
    return seen


def via_grid(r):
    grid = [fresh("r0"), fresh("r1")]
    seen = []
    for row in grid:
        for v in row:
            grid[0] = None              # the row goes; the outer loop's local still holds it
            row = None                  # and now nothing but v holds the item
            seen.append((v.k, v.k in LOG))       # its finalizer must not have run yet
            break
    return seen


def via_rebind(r):
    seen = []
    for k in range(4):
        ys = fresh(f"y{k}", 2)          # the list's only holder is ys
        v = ys[0]
        ys = None                       # the list goes, and with it its reference to v
        seen.append((v.k, v.k in LOG))
    return seen


def via_del_list(r):
    seen = []
    for k in range(4):
        ys = fresh(f"z{k}", 2)
        v = ys[1]
        del ys                          # the same through del
        seen.append((v.k, v.k in LOG))
    return seen


def plain_sum(r):
    xs = list(range(1000, 1100))
    s = 0
    for v in xs:
        s += v
    ps = [(i, i + 1) for i in range(50)]
    for a, b in ps:
        s += a - b
    return s


for name, f in [("call", via_call), ("store", via_store), ("del", via_del), ("finalizer", via_finalizer),
                ("eq", via_eq), ("hash", via_hash), ("bool", via_bool), ("handler", via_handler),
                ("generator", via_generator), ("tuple", via_tuple), ("grid", via_grid), ("rebind", via_rebind),
                ("dellist", via_del_list), ("sum", plain_sum)]:
    check(name, f)
