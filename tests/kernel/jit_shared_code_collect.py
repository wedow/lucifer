# Namespaces that run a function of one code object, the way a server runs
# a handler per request: exec of source compiled anew, or of one compiled
# code, into a fresh namespace, the handler called hot or once, the
# namespace dropped. gc.collect() frees each namespace as CPython does: a
# finalizer object stored in each counts the ones freed. Each output line
# must be the interpreter's.
#
# Not covered yet, so the shared-code case is left out under
# -X lucifer_jit=always, where every handler compiles on its first call: a
# unit of a code that stays alive keeps the namespace it was built for
# alive with the code (luc-k7ns).
import gc
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

freed = []


class Witness:
    def __init__(self, tag):
        self.tag = tag

    def __del__(self):
        freed.append(self.tag)


SRC = '''
def handler(req):
    s = 0
    for i in range(req):
        s += i * SCALE
    return s
'''
CODE = compile(SRC, 'handlers', 'exec')


def serve(code_of, requests, calls):
    total = 0
    for k in range(requests):
        ns = {'SCALE': k, 'witness': Witness(k)}
        ns['self_ref'] = ns         # a cycle, as a module's namespace has
        exec(code_of(), ns)
        h = ns['handler']
        for _ in range(calls):
            total += h(50)
        del ns, h
    return total


def report(name, total, requests):
    gc.collect()
    gc.collect()
    print(name, total, len(freed), sorted(freed) == list(range(requests)))
    freed.clear()


ALWAYS = sys._xoptions.get('lucifer_jit') == 'always'
# Source compiled anew per request, each handler hot: code, its units,
# function and namespace are one cycle. Left out wherever every function
# compiles at its first call (luc-k7ns): serve's outer-loop unit, built in
# the first request, inlines that request's handler as its learned callee
# (guarded by identity) and keeps that handler and its namespace. It leaves
# each later request at the inlined compile, before the guard, so it never
# pays a loss there; a learned rebuild kills only the unit that paid (the
# inner loop's), and the outer one lives on.
if not ALWAYS:
    report('fresh code, hot', serve(lambda: compile(SRC, 'handlers', 'exec'), 20, N // 10), 20)
# One code object, its first namespace's handler hot, then a namespace per
# request whose handler runs once: those stay cold, so nothing keeps them.
first = {'SCALE': 1}
exec(CODE, first)
print('first', sum(first['handler'](50) for _ in range(N // 10)))
if not ALWAYS:
    report('shared code, cold', serve(lambda: CODE, 200, 1), 200)
