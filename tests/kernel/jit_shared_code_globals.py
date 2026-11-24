# Functions that share one code object but not their globals or builtins
# (types.FunctionType(code, other_globals), or a module imported twice
# from the same compiled code): compiled code made for one must not answer
# with the other's globals. Each case runs one function hot, then the
# other, at a call's entry and at a loop's. Each output line must be the
# interpreter's.
import builtins
import sys
import types

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Mark:
    def __repr__(self):
        return 'Mark'


SRC = '''
def merge(args):
    order = []
    j = len(args)
    for i, a in enumerate(args):
        if a is MARK:
            order.append(j)
            j += 1
        else:
            order.append(i)
    return order

def scale(n):
    s = 0
    for i in range(n):
        s += i * K
    return s

def count(xs):
    return len(xs)

def bump():
    global TOTAL
    TOTAL += 1
'''


def module(k):
    g = {'MARK': Mark(), 'K': k, 'TOTAL': 0}
    exec(SRC, g)
    return g


g1 = module(1)
g2 = module(2)
g2_funcs = {name: types.FunctionType(g1[name].__code__, g2) for name in ('merge', 'scale', 'count', 'bump')}
print(g2_funcs['merge'].__code__ is g1['merge'].__code__)

# Constants folded from the globals: one function hot, then the other.
for _ in range(N):
    r1 = g1['merge']((g1['MARK'], 0, g1['MARK'], 1))
print('merge g1', r1)
print('merge g2', g2_funcs['merge']((g2['MARK'], 0, g2['MARK'], 1)))
for _ in range(N):
    r2 = g2_funcs['merge']((g2['MARK'], 0, g2['MARK'], 1))
print('merge g2 hot', r2, g1['merge']((g1['MARK'], 0, g1['MARK'], 1)))
# Alternating calls.
res = set()
for i in range(N):
    g = g1 if i & 1 else g2
    f = g1['merge'] if i & 1 else g2_funcs['merge']
    res.add(tuple(f((g['MARK'], 0, g['MARK'], 1))))
print('alternating', sorted(res))

# A loop entered hot in one function, then the other's.
print('scale', g1['scale'](N * 10), g2_funcs['scale'](N * 10), g1['scale'](N * 10))

# Global stores land in the function's own globals.
for _ in range(N):
    g1['bump']()
for _ in range(N // 2):
    g2_funcs['bump']()
print('totals', g1['TOTAL'], g2['TOTAL'])

# Builtins: the same code with a builtins table whose len differs.
fake = dict(vars(builtins))
fake['len'] = lambda xs: -1
g3 = {'__builtins__': fake}
count3 = types.FunctionType(g1['count'].__code__, g3)
for _ in range(N):
    c1 = g1['count']((1, 2, 3))
print('count', c1, count3((1, 2, 3)))
for _ in range(N):
    c3 = count3((1, 2, 3))
print('count hot', c3, g1['count']((1, 2, 3)))
