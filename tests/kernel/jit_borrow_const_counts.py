# A loop's carried value that starts as a constant and then takes a list
# item, where compiled code may hold the items without counting them. The
# constant's count must come back to where it was after each call: a
# constant handed to the carried value on an edge into the loop, or on an
# edge inside it, takes a count there only if the carried value's release
# gives it back. The count of each constant is compared before and after
# many calls made once the code is warm, so a count left behind once per
# loop entry or once per iteration shows as growth with the calls, and a
# count released that was never taken as loss (the compiled code's own
# references to its constants are a few at most). The shapes: the constant
# on the edge into the loop, on an edge inside it, feeding two values at
# one join, shared on one edge by a value compiled code counts (it lives
# across a call) and one it does not, given back on the back edge, beside
# a tuple's items, and beside an immortal constant.
import sys

BIG = -4611686018427387904      # an int outside the small-int cache: a mortal constant


def biggest(xs):
    m = -4611686018427387904
    for v in xs:
        if v > m:
            m = v
    return m > 0


def counted(xs):
    m = -4611686018427387904
    n = 0
    for v in xs:
        if v > m:
            m = v
            n += 1
    return n


def biggest_f(xs):
    m = -1.5
    for v in xs:
        if v > m:
            m = v
    return m > 0


def each_turn(xs):
    s = 0
    for v in xs:
        w = v if v & 1 else -4611686018427387904
        if w > s:
            s += 1
    return s


def two_at_one_join(xs):
    s = 0
    for v in xs:
        if v & 1:
            a = v
            b = v
        else:
            a = -4611686018427387904
            b = -4611686018427387904
        if a > s:
            s += 1
        if b > s:
            s += 1
    return s


def counted_beside(xs):
    s = 0
    out = []
    for v in xs:
        if v & 1:
            a = v
            b = v
        else:
            a = -4611686018427387904
            b = -4611686018427387904
        if a > s:
            s += 1
        out.append(0)           # b lives across the call: counted
        if b > s:
            s += 1
    return s


def back_edge(xs):
    m = -4611686018427387904
    n = 0
    for v in xs:
        if v > m:
            m = v
            n += 1
        if v % 5 == 0:
            m = -4611686018427387904
    return n


def tuple_items(pairs):
    n = 0
    for a, b in pairs:
        m = a if a > b else -4611686018427387904
        if m > n:
            n += 1
    return n


def beside_immortal(xs):
    s = 0
    for v in xs:
        w = v if v & 1 else -4611686018427387904 if v & 2 else 0
        if w > s:
            s += 1
    return s


def const_of(f, value):
    for k in f.__code__.co_consts:
        if type(k) is type(value) and k == value:
            return k
    raise LookupError(value)


def growth(f, xs, value, calls=3000):
    k = const_of(f, value)
    for _ in range(calls):
        f(xs)
    before = sys.getrefcount(k)
    for _ in range(calls):
        f(xs)
    return abs(sys.getrefcount(k) - before) < calls // 10


ints = list(range(1, 40))
floats = [float(i) for i in range(1, 40)]
print("entry int", growth(biggest, ints, BIG))
print("entry counted", growth(counted, ints, BIG))
print("entry float", growth(biggest_f, floats, -1.5))
print("each turn", growth(each_turn, ints, BIG))
print("two at one join", growth(two_at_one_join, ints, BIG))
print("counted beside", growth(counted_beside, ints, BIG))
print("back edge", growth(back_edge, ints, BIG))
print("tuple items", growth(tuple_items, [(i, 40 - i) for i in range(1, 40)], BIG))
print("beside immortal", growth(beside_immortal, ints, BIG))
