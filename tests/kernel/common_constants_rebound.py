# LOAD_COMMON_CONSTANT names the builtins as the interpreter started with
# them (CPython's common_consts): rebinding builtins.all, any or tuple does
# not change what all(genexpr), any(genexpr), tuple(genexpr), assert and
# NotImplementedError load.
import builtins

orig = builtins.all, builtins.any, builtins.tuple


def loads(xs):
    a = all(x > 0 for x in xs)
    b = any(x > 2 for x in xs)
    t = tuple(x * 2 for x in xs)
    return a, b, t


print(loads([1, 2, 3]))
builtins.all = lambda it: "rebound all"
builtins.any = lambda it: "rebound any"
builtins.tuple = lambda it: "rebound tuple"
try:
    for i in range(300):
        r = loads([1, 2, 3])
    print(r)
    print(all([1, 0]), any([0]), tuple([1]))
finally:
    builtins.all, builtins.any, builtins.tuple = orig
del builtins.all
try:
    print(loads([0, 5]))
finally:
    builtins.all = orig[0]
try:
    assert False, "message"
except AssertionError as e:
    print("assert", e)
try:
    raise NotImplementedError
except NotImplementedError:
    print("not implemented")
