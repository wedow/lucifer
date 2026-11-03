# chained comparisons, assert, loops with else, break and continue in try/finally.
def compare(a, b, c):
    return a < b < c, a <= b == c, a != b != c, a is b is c, a in [b] in [[c]]


print(compare(1, 2, 3), compare(1, 1, 1), compare(3, 2, 1))


class Noisy:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        print("lt", self.v, getattr(other, "v", other))
        return self.v < getattr(other, "v", other)

    def __gt__(self, other):
        print("gt", self.v, getattr(other, "v", other))
        return self.v > getattr(other, "v", other)


print(Noisy(1) < Noisy(2) < Noisy(3))
print(Noisy(5) < Noisy(2) < Noisy(3))
print(0 < Noisy(1) > -1)


def evaluated_once():
    log = []

    def f(n):
        log.append(n)
        return n

    print(f(1) < f(2) < f(3), f(5) < f(2) < f(9), log)


evaluated_once()


def asserts():
    assert True
    assert 1 == 1, "never"
    try:
        assert 1 == 2
    except AssertionError as e:
        print("AssertionError", repr(e.args))
    try:
        assert False, "message"
    except AssertionError as e:
        print("AssertionError", e.args)
    try:
        assert (), ("tuple", "message")
    except AssertionError as e:
        print(e.args)
    calls = []
    assert calls.append(1) is None, calls.append(2)
    print(calls)


asserts()


def loops():
    for i in range(3):
        pass
    else:
        print("for else", i)
    for i in range(5):
        if i == 2:
            break
    else:
        print("not printed")
    print("after break", i)
    n = 0
    while n < 3:
        n += 1
        if n == 2:
            continue
    else:
        print("while else", n)
    while True:
        break
    else:
        print("not printed")
    for i in []:
        pass
    else:
        print("empty else")
    for a in range(3):
        for b in range(3):
            if b == 1:
                break
        else:
            print("inner else")
            continue
        print("outer", a, b)


loops()


def try_loops():
    log = []
    for i in range(4):
        try:
            if i == 1:
                continue
            if i == 3:
                break
            log.append(("body", i))
        finally:
            log.append(("finally", i))
    else:
        log.append("else")
    n = 0
    while n < 3:
        n += 1
        try:
            try:
                if n == 2:
                    continue
                log.append(("w", n))
            finally:
                log.append(("inner", n))
        finally:
            log.append(("outer", n))
    return log


print(try_loops())


def return_in_finally():
    for i in range(3):
        try:
            return "try", i
        finally:
            print("cleanup", i)


def finally_overrides():
    for i in range(2):
        try:
            raise ValueError(i)
        finally:
            continue
    return "swallowed"


def break_in_finally():
    while True:
        try:
            return "lost"
        finally:
            break
    return "break wins"


def with_break():
    class CM:
        def __enter__(self):
            return self

        def __exit__(self, *args):
            print("exit", args[0])
            return False

    for i in range(3):
        with CM():
            if i == 1:
                break
            continue
    return i


print(return_in_finally(), finally_overrides(), break_in_finally(), with_break())


def ternaries_and_bool(x):
    a = "pos" if x > 0 else "neg" if x < 0 else "zero"
    b = x and "truthy" or "falsy"
    c = not x
    d = x > 0 and x < 10 or x == -1
    return a, b, c, d


for v in (5, 0, -1, 50):
    print(ternaries_and_bool(v))


def iter_protocol():
    it = iter([1, 2, 3])
    for x in it:
        print(x)
        if x == 2:
            break
    print(list(it))
    d = {"a": 1}
    for k in d:
        d["b" if k == "a" else k] = 2
        break
    print(sorted(d))


iter_protocol()
for i, ch in enumerate("ab", 1):
    print(i, ch)
print([(a, b) for a in range(3) for b in range(3) if a < b != 2])
else_count = sum(1 for _ in range(10) if _ % 3 == 0)
print(else_count)
