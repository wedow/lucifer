# Namespaces that run a function of one code object, the way a server runs
# a handler per request: exec of source compiled anew, or of one compiled
# code, into a fresh namespace, the handler called hot or once, the
# namespace dropped. gc.collect() frees each namespace as CPython does: a
# finalizer object stored in each counts the ones freed. Each output line
# must be the interpreter's.
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


# Source compiled anew per request, each handler hot: code, its units,
# function and namespace are one cycle. Where every function compiles at
# its first call, serve's outer-loop unit, built in the first request,
# inlines that request's handler as its learned callee (guarded by
# identity): that unit must not keep the handler's namespace.
report('fresh code, hot', serve(lambda: compile(SRC, 'handlers', 'exec'), 20, N // 10), 20)
# One code object, its first namespace's handler hot, then a namespace per
# request whose handler runs once: those stay cold, so nothing keeps them.
first = {'SCALE': 1}
exec(CODE, first)
print('first', sum(first['handler'](50) for _ in range(N // 10)))
report('shared code, cold', serve(lambda: CODE, 200, 1), 200)
