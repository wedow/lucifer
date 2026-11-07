# Calls C makes into compiled Python functions: a dict's lookups call its
# keys' __hash__ and __eq__ (one with a defaulted parameter, one that
# raises on a marked value). tools/jitcheck
# runs this at two sizes and requires that no call per iteration enters
# compiled code through the eval loop's C entry (its direct calls): C
# enters the callee's unit itself.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Key:
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return self.v

    def __eq__(self, other, strict=True):
        if self.v < 0:
            raise ValueError('marked')
        return self.v == other.v


def hot(n):
    d = {Key(i): i for i in range(8)}
    probe = [Key(i) for i in range(8)]
    s = 0
    for i in range(n):
        s += d[probe[i & 7]]
    return s


print(hot(N))
try:
    {Key(-1): 0}[Key(-1)]
except ValueError as e:
    print('ValueError', e)
print(hot(3))
