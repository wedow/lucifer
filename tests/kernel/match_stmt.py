# match statements: literal, capture, sequence, mapping, class, or, guard and as patterns.
from _collections import defaultdict, deque


def literal(x):
    match x:
        case 0:
            return "zero"
        case 1 | 2 | 3:
            return "small"
        case "a" | "b":
            return "letter"
        case None:
            return "none"
        case True:
            return "true"
        case -1.5:
            return "float"
        case 1 + 2j:
            return "complex"
        case b"x":
            return "bytes"
        case _:
            return "other"


for v in (0, 1, 3, "a", "c", None, True, False, -1.5, 1 + 2j, b"x", [], 99):
    print(repr(v), literal(v))


def sequence(x):
    match x:
        case []:
            return "empty"
        case [a]:
            return f"one {a}"
        case [a, b]:
            return f"two {a} {b}"
        case [a, *rest, z]:
            return f"many {a} {rest} {z}"
        case (1, 2):
            return "unreachable"
        case _:
            return "no"


for v in ([], [1], (1, 2), [1, 2, 3, 4], range(5), "ab", b"ab", {1: 2}, 7, deque([1, 2]), iter([1])):
    print(sequence(v))


def star(x):
    match x:
        case [*_]:
            return "any sequence"
        case _:
            return "not a sequence"


print(star([1]), star("s"), star(bytearray(b"s")), star(memoryview(b"ab")), star(range(2)))


class Seq(list):
    pass


class Map(dict):
    pass


print(sequence(Seq([4, 5])))


def mapping(x):
    match x:
        case {}:
            return "any mapping"
        case {"k": 1}:
            return "unreachable"


def mapping2(x):
    match x:
        case {"a": 1, "b": b, **rest}:
            return f"ab {b} {rest}"
        case {"a": a}:
            return f"a {a}"
        case {1: _, 2: _}:
            return "ints"
        case _:
            return "no"


for v in ({"a": 1, "b": 2, "c": 3}, {"a": 5}, {1: 1, 2: 2}, Map(a=1, b=0), defaultdict(int, a=3), [], "a"):
    print(mapping2(v))
print(mapping({}), mapping([]))


class Defaulting(dict):
    def __missing__(self, key):
        return "missing"


print(mapping2(Defaulting(a=2)))


def klass(x):
    match x:
        case int(n) if n > 10:
            return f"big int {n}"
        case int():
            return "int"
        case str(s):
            return f"str {s}"
        case bool():
            return "unreachable"
        case list([a, b]):
            return f"pair {a} {b}"
        case tuple():
            return "tuple"
        case float(f):
            return f"float {f}"
        case _:
            return "other"


for v in (5, 50, True, "s", [1, 2], [1], (1,), 1.5, None, b"b"):
    print(klass(v))


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x, self.y = x, y

    def __repr__(self):
        return f"Point({self.x}, {self.y})"


class Vec:
    __match_args__ = ("a", "b")

    def __init__(self, a, b):
        self.a, self.b = a, b


class Plain:
    pass


def shape(p):
    match p:
        case Point(0, 0):
            return "origin"
        case Point(x, 0):
            return f"x-axis {x}"
        case Point(x=0, y=y):
            return f"y-axis {y}"
        case Point(x, y):
            return f"point {x} {y}"
        case Vec(a, b=2):
            return f"vec {a}"
        case Vec(a, b):
            return f"vec {a} {b}"
        case Plain():
            return "plain"
        case _:
            return "unknown"


for v in (Point(0, 0), Point(3, 0), Point(0, 4), Point(1, 2), Vec(1, 2), Vec(1, 3), Plain(), 3):
    print(shape(v))


def errors(subject, pattern_class):
    def run():
        match subject:
            case pattern_class(1, 2, 3):
                return "matched"
            case _:
                return "no"
    try:
        return run()
    except TypeError as e:
        return f"TypeError: {e}"


print(errors(Vec(1, 2), Vec))
print(errors(Vec(1, 2), 3))
print(errors(1, int))


class BadArgs:
    __match_args__ = ["a"]


class BadName:
    __match_args__ = (1,)


def bad(subject, cls):
    try:
        match subject:
            case cls(_):
                return "matched"
    except TypeError as e:
        return f"TypeError: {e}"
    return "no"


print(bad(BadArgs(), BadArgs), bad(BadName(), BadName))


def duplicate(p):
    try:
        match p:
            case Vec(a, a=x):
                return "matched"
    except TypeError as e:
        return f"TypeError: {e}"


print(duplicate(Vec(1, 2)))


class Missing:
    __match_args__ = ("nope",)


def missing(p):
    match p:
        case Missing(x):
            return "matched"
        case _:
            return "no attribute"


print(missing(Missing()))


def as_guard(x):
    match x:
        case [int() as a, int() as b] if a < b:
            return f"ascending {a} {b}"
        case [(1 | 2) as a, _] | [_, (1 | 2) as a]:
            return f"has one or two {a}"
        case str() as s if s:
            return f"non-empty {s}"
        case _:
            return "fallback"


for v in ([1, 2], [2, 1], [5, 2], [9, 9], "hi", ""):
    print(as_guard(v))


def dotted(x):
    match x:
        case Color.RED:
            return "red"
        case Color.BLUE:
            return "blue"


class Color:
    RED = "r"
    BLUE = "b"


print(dotted("r"), dotted("b"), dotted("g"))


def nested(x):
    match x:
        case {"pts": [Point(x=0) as p, *others], "n": int(n)}:
            return f"{p} {others} {n}"
        case _:
            return "no"


print(nested({"pts": [Point(0, 1), Point(2, 3)], "n": 2}))
print(nested({"pts": [Point(1, 1)], "n": 2}))


def subject_is_evaluated_once():
    calls = []

    def f():
        calls.append(1)
        return (1, 2)

    match f():
        case (3, _):
            pass
        case (1, 3):
            pass
        case (1, 2):
            pass
    return len(calls)


print(subject_is_evaluated_once())


def mapping_key_error(x):
    try:
        match x:
            case {Color.RED: 1, Color.RED: 2}:
                pass
    except ValueError as e:
        return str(e)
    return "ok"


print(mapping_key_error({}))
print(mapping_key_error({"r": 1, "s": 2}))
