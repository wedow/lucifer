import builtins
import gc
import sys


def walk_referents(*roots):
    # Keep visited objects alive so their ids cannot be reused during the walk.
    seen = {}
    stack = list(roots)
    while stack:
        obj = stack[-1]
        del stack[-1]
        key = id(obj)
        if key not in seen:
            seen[key] = obj
            stack.extend(gc.get_referents(obj))
    return len(list(seen.values())) == len(seen)


# A walk through the startup heap must yield values usable in ordinary containers.
print("walk:", walk_referents(builtins, sys))

# Ordinary singleton values and user markers are still referents.
values = (None, NotImplemented, Ellipsis, object())
refs = gc.get_referents(values)
print("values:", set(refs) == set(values), refs.pop() in values)
