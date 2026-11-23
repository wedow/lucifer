# Hot loops over list and tuple items whose items nothing can free before
# their last use: a loop over a list of ints, reads of instances'
# attributes, a method call over a list of instances, and a loop unpacking
# pairs. tools/jitcheck runs this at two sizes under -X lucifer_jit_stats
# and requires that compiled code executes no INCREF or DECREF per
# iteration: each item is read without a count, as the list or tuple keeps
# it. The loops return nothing they read (a returned item is counted) and
# carry no number between turns (a carried int's or float's box is released
# each turn until number origins go uncounted too, luc-5bi7 stage 3); each
# outer turn makes and releases one list iterator, the one release the lane
# allows (1/2 per iteration).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class B:
    def __init__(self, i):
        self.x = i
        self.y = i + 1

    def get(self):
        return self.x


def items(n, xs):
    for _ in range(n):
        for v in xs:
            pass
    return n


def loads(n, bs):
    for _ in range(n):
        for b in bs:
            b.x + b.y
    return n


def calls(n, bs):
    for _ in range(n):
        for b in bs:
            b.get()
    return n


def tupunpack(n, ps):
    for _ in range(n):
        for a, b in ps:
            pass
    return n


xs = list(range(1000, 1010))
bs = [B(i) for i in range(10)]
ps = [(i, i + 1) for i in range(10)]
k = N // 10
print(items(k, xs), loads(k, bs), calls(k, bs), tupunpack(k, ps))
