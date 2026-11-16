# eval and exec of code whose globals have no __builtins__ take the
# calling frame's builtins. Hot, the caller is compiled, and it may be an
# inlined frame of a module with builtins of its own, a function the unit
# holds as a value, or a generator's frame: each must see its own.
import builtins
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000

custom = dict(vars(builtins))
custom['len'] = lambda x: -1
ns = {'__builtins__': custom}
exec("def probe(src):\n"
     "    return eval(src, {})\n"
     "def run(src):\n"
     "    g = {}\n"
     "    exec(src, g)\n"
     "    return g['r']\n"
     "def each(srcs):\n"
     "    for s in srcs:\n"
     "        yield eval(s, {})\n", ns)
probe, run, each = ns['probe'], ns['run'], ns['each']


def mine(src):
    return eval(src, {})


def outer(n, f):
    t = 0
    for i in range(n):
        t += probe("len('ab')") + mine("len('abc')") + run("r = len('abcd')") + f("len('a')")
        t += sum(each(["len('xy')", "len('xyz')"]))
    return t


print(outer(N, mine), outer(N, probe))
g = {}
exec("b = __builtins__", g)
print(type(g['b']).__name__, g['b'] is vars(builtins) or g['b'] is builtins)
