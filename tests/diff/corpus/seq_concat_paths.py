"""list + list and tuple + tuple in hot loops (empty sides, a sequence
with itself, results kept and changed after), beside the mixed and
subclass cases that keep their own paths (list + tuple's error, a list
subclass's __add__, += in place)."""
from support import loops


class L(list):
    def __add__(self, other):
        return 'L.__add__'


def cat(a, b):
    return a + b


def run():
    acc = []
    t = ()
    for i in range(300):
        acc = cat(acc, [i])[-5:]
        t = cat(t, (i,))[-5:]
        same = cat(acc, acc)
        same.append('x')
    out = [acc, t, len(same), cat([], []), cat((), ()), cat(L([1]), [2]), cat([1], L([2]))]
    for a, b in (([1], (2,)), ((1,), [2])):
        try:
            cat(a, b)
        except TypeError as e:
            out.append(str(e))
    x = [1]
    y = x
    x += [2]
    out.append((x, y is x))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
