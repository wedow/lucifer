# Functions that share one code object but not their globals or builtins
# (types.FunctionType(code, other_globals), or a module imported twice
# from the same compiled code): compiled code made for one must not answer
# with the other's globals. Each case runs one function hot, then the
# other, at a call's entry and at a loop's, called from module code, from
# a compiled caller that calls it out of line (its direct entry) or in line
# (a callee known by its code), as generators, and after importlib.reload.
# Each output line must be the interpreter's.
import builtins
import importlib
import os
import shutil
import sys
import tempfile
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

def depth(n):
    if n == 0:
        return 0
    return K + depth(n - 1)

def tagged(x):
    return x * K

def gen(n):
    for i in range(n):
        yield i * K
'''


def module(k):
    g = {'MARK': Mark(), 'K': k, 'TOTAL': 0}
    exec(SRC, g)
    return g


g1 = module(1)
g2 = module(2)
g2_funcs = {name: types.FunctionType(g1[name].__code__, g2) for name in ('merge', 'scale', 'count', 'bump', 'depth', 'tagged', 'gen')}
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


# A compiled caller reaching each function out of line (recursive: its
# direct entry), one hot first, then both.
def call_depth(f, n):
    s = 0
    for _ in range(n):
        s += f(5)
    return s


print('direct', call_depth(g1['depth'], N), call_depth(g2_funcs['depth'], N))
def call_both(n):
    s = 0
    for i in range(n):
        s += (g1['depth'] if i & 1 else g2_funcs['depth'])(3)
    return s


print('direct both', call_both(N))

# A compiled caller whose callee is known by its code: two functions of it
# with different globals, one site.
def call_tagged(fs, n):
    s = 0
    for i in range(n):
        s += fs[i & 1](i)
    return s


print('by code', call_tagged([g1['tagged'], g1['tagged']], N), call_tagged([g1['tagged'], g2_funcs['tagged']], N),
      call_tagged([g2_funcs['tagged'], g1['tagged']], N))

# Generators of one code with different globals.
def drain(f, n):
    s = 0
    for _ in range(n // 10):
        for v in f(10):
            s += v
    return s


print('gens', drain(g1['gen'], N), drain(g2_funcs['gen'], N), drain(g1['gen'], N))

# importlib.reload of a module with a hot function: the old function keeps
# running beside the new one.
tmp = tempfile.mkdtemp()
with open(os.path.join(tmp, 'shared_code_mod.py'), 'w') as fh:
    fh.write('SCALE = 3\n\ndef work(n):\n    s = 0\n    for i in range(n):\n        s += i * SCALE\n    return s\n')
sys.path.insert(0, tmp)
sys.dont_write_bytecode = True
import shared_code_mod
old_work = shared_code_mod.work
r1 = [old_work(N) for _ in range(20)][-1]
with open(os.path.join(tmp, 'shared_code_mod.py'), 'w') as fh:
    fh.write('SCALE = 5\n\ndef work(n):\n    s = 0\n    for i in range(n):\n        s += i * SCALE\n    return s\n')
importlib.invalidate_caches()
importlib.reload(shared_code_mod)
r2 = [shared_code_mod.work(N) for _ in range(20)][-1]
print('reload', r1, r2, old_work(N), shared_code_mod.SCALE)
shutil.rmtree(tmp)
