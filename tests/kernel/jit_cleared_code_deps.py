# Two codes in one garbage cycle, one's units inlining the other (outer
# calls inner), both relying on a class's version; the namespace holds an
# object whose finalizer changes that class while the collector clears
# the cycle, so the invalidation reaches outer's units after inner's code
# may have been cleared. A live function relies on the same class and
# runs after. Each output line must be the interpreter's.
import gc
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class T:
    attr = 1


class Bump:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        T.attr = self.k


SRC = '''
def inner(o):
    return o.attr + K

def outer(o, n):
    s = 0
    for i in range(n):
        s += inner(o)
    return s
'''


def live(o, n):
    s = 0
    for i in range(n):
        s += o.attr
    return s


for k in range(20):
    ns = {'K': k, 'bump': Bump(k + 2)}
    ns['self'] = ns
    exec(compile(SRC, 'handlers', 'exec'), ns)
    r = ns['outer'](T(), N // 10)
    del ns
    gc.collect()
    print(k, r, T.attr, live(T(), N // 10))
