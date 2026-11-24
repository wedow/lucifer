# Two functions of one code object with different globals, each hot in
# turn and called from module code (each call enters a unit of its own
# code): the first a fixed number of times, the second as many times as
# the size asks. Each has units of its own, so the second's calls run its
# loop in compiled code, and neither enters the other's unit.
import sys
import types

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

SRC = '''
def work(n):
    s = 0
    for i in range(n):
        s += i * K
    return s
'''
g1 = {'K': 1}
exec(SRC, g1)
g2 = {'K': 2}
work2 = types.FunctionType(g1['work'].__code__, g2)


s1 = s2 = 0
for _ in range(100):
    s1 += g1['work'](10)
for _ in range(N):
    s2 += work2(3)
for _ in range(100):
    s1 += g1['work'](10)
print(s1, s2)
