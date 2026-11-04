"""Slices and negative indexes of exact lists, tuples, strs and bytes taken
by the eval loop agree with the slice protocol on every kind of bound."""

import itertools

lst = list(range(7))
tup = tuple(range(7))
text = "abcdefg"
data = b"abcdefg"
bounds = [None, 0, 1, 3, 6, 7, 8, -1, -3, -7, -8, -100, 100, 2**70, -2**70, True, False]
steps = [None, 1, 2, 3, -1, -2, -9, 100, 2**70, -2**70, True]

n = 0
for seq in (lst, tup, text, data, bytearray(data), [], (), "", b""):
    for a, b in itertools.product(bounds, bounds):
        got = seq[a:b]
        n += len(got)
        print(1, type(seq).__name__, a, b, got)
    for a, b, c in itertools.product(bounds[::3], bounds[::3], steps):
        print(2, type(seq).__name__, a, b, c, seq[a:b:c])
print(3, n)
for seq in (lst, tup, text, data):
    for step in (0, 0.5, "x", None):
        try:
            print(4, seq[::step])
        except (TypeError, ValueError) as ex:
            print(4, type(ex).__name__, ex)
    for bad in (1.5, "1", object()):
        for how in (lambda: seq[bad:], lambda: seq[:bad]):
            try:
                how()
            except TypeError as ex:
                print(5, ex)


class Ix:
    def __index__(self):
        return 2


print(6, lst[Ix():], tup[:Ix()], text[Ix():Ix().__index__() + 2], data[::Ix()])
t = (1, 2, 3)
print(7, t[:] is t, t[0:3] is t, text[:] is text, data[:] is data, lst[:] is lst, t[:2] is t)


class L(list):
    pass


class T(tuple):
    pass


class S(str):
    pass


class B(bytes):
    pass


for cls, v in ((L, [1, 2, 3]), (T, (1, 2, 3)), (S, "abc"), (B, b"abc")):
    x = cls(v)
    print(8, type(x[1:]).__name__, x[1:], x[:] == x, type(x[:]).__name__, x[-1])
for seq in (lst, tup):
    for i in (-1, -7, -8, 0, 6, 7, 2**70, -2**70, True):
        try:
            print(9, seq[i])
        except IndexError as ex:
            print(9, ex)
for i in (-1, -7, -8, 7):
    for seq in (text, data):
        try:
            print(10, seq[i])
        except IndexError as ex:
            print(10, ex)
s = slice(1, None, 2)
print(11, lst[s], tup[s], text[s], data[s], lst[slice(None)], lst[slice(1, 2, 3)])
big = list(range(100))
print(12, big[10:90:7], big[::-7], big[-5:], big[:-95], text[2:5], "é€😀x"[1:3], "é€😀x"[::-1])
x = [1, 2, 3]
y = x[:]
y.append(4)
print(13, x, y)
