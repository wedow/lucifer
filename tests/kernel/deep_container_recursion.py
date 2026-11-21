# Builtins on nested containers recurse in C in CPython, bounded by the C
# stack, not by sys.getrecursionlimit(); only Python frames count against the
# limit. Kernel code stands in for that C: 400 levels work at any limit, and
# user recursion through builtins reaches the depth CPython's does.
import copy
import json
import sys


def nest(kind, depth):
    value = 1
    for _ in range(depth):
        value = kind(value)
    return value


kinds = {'list': lambda v: [v], 'tuple': lambda v: (v,), 'dict': lambda v: {'k': v}}
for limit in (sys.getrecursionlimit(), 80):
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(limit)
    try:
        for name, kind in kinds.items():
            a, b = nest(kind, 400), nest(kind, 400)
            text = repr(a)
            row = [name, len(text), str(a) == text, a == b, a != b]
            row.append(len(json.dumps(a)))
            if name == 'tuple':
                row.append(hash(a) == hash(b))
            if limit > 80:
                row.append(copy.deepcopy(a) == a)
            print(limit, *row)
    finally:
        sys.setrecursionlimit(old)


def frames():
    n, f = 0, sys._getframe(1)
    while f is not None:
        n, f = n + 1, f.f_back
    return n


def reach(step):
    """How deep `step` recurses with 60 frames left under the limit."""
    reached = [0]
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(frames() + 60)
    try:
        step(0, reached)
    except RecursionError as e:
        message = str(e)
    finally:
        sys.setrecursionlimit(old)
    return reached[0], message


def plain(n, reached):
    reached[0] = n
    plain(n + 1, reached)


class Node:
    def __init__(self, n, reached):
        self.n, self.reached = n, reached

    def __repr__(self):
        self.reached[0] = self.n
        return repr([Node(self.n + 1, self.reached)])


def through_repr(n, reached):
    repr(Node(n, reached))


def through_sorted(n, reached):
    reached[0] = n
    sorted([0, 1], key=lambda x: through_sorted(n + 1, reached))


def through_map(n, reached):
    reached[0] = n
    list(map(lambda x: through_map(n + 1, reached), [0]))


for step in (plain, through_repr, through_sorted, through_map):
    print(step.__name__, *reach(step))

# Past what the stack holds, a RecursionError, not a crash.
for name, kind in kinds.items():
    a, b = nest(kind, 100000), nest(kind, 100000)
    for what, op in (('repr', repr), ('eq', lambda x: x == b)):
        try:
            op(a)
            print(name, what, 'no error')
        except RecursionError:
            print(name, what, 'RecursionError')
