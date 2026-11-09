# Slices of str, bytes, lists and tuples by bounds the code computes
# (luc-14n0): the bounds are machine ints compiled code never boxes.
# Negative, clamped, empty and None bounds, every container kind, and
# containers the native slice does not take (a str subclass, a range, a
# class with __getitem__) must give the interpreter's results.
# tools/jitcheck runs this at two sizes and requires that the ints
# compiled code allocates do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

TEXT = ''.join(chr(97 + i % 26) for i in range(3000))
DATA = TEXT.encode()
ITEMS = list(range(3000))
TUPLE = tuple(ITEMS)


class Seq:
    def __getitem__(self, s):
        return (s.start, s.stop, s.step)


class Str(str):
    pass


def window(seq, start, width):
    return seq[start + 300:start + 300 + width]


def tails(seq, k):
    return seq[k + 400:], seq[:k + 500], seq[-k - 700:-k - 300]


def hot(n):
    total = 0
    for i in range(n):
        k = i % 200  # a cached int: the bounds made from it are not
        for seq in (TEXT, DATA, ITEMS, TUPLE):
            a, b, c = tails(seq, k)
            # kept small: the total is a cached int, so the ints counted are the bounds'
            total = (total + len(window(seq, k, 7)) + len(a) + len(b) + len(c)) % 251
    return total


print(hot(N))
for seq in (TEXT, DATA, ITEMS[:20], TUPLE[:20], Str(TEXT[:20]), range(3000), Seq()):
    print(type(seq).__name__, window(seq, -5, 3), window(seq, 2800, 5), window(seq, -9000, 4),
          window(seq, 10, -3))
print(window(TEXT, 2 ** 62, 2 ** 62), window(TEXT, -2 ** 62, 1))
print(type(window(Str(TEXT), 0, 3)).__name__, tails(TUPLE, 1)[2][:3])
