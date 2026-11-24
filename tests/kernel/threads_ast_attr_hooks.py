# One thread gives an ast node class and takes away its own
# __getattribute__, __getattr__ and __setattr__, with no synchronisation,
# while another reads, writes and misses attributes of its instances, by
# the bytecodes and by getattr: every answer is the C slot's or the hook's.
import ast
import threading


class Sub(ast.Name):
    pass


def hook_getattribute(self, name):
    v = object.__getattribute__(self, name)
    return 'hooked' if name == 'id' else v


def hook_getattr(self, name):
    return 'missing'


def hook_setattr(self, name, value):
    object.__setattr__(self, name, -value if type(value) is int else value)


hooks = (('__getattribute__', hook_getattribute), ('__getattr__', hook_getattr),
         ('__setattr__', hook_setattr))
stop = threading.Event()


def flip():
    k = 0
    while not stop.is_set():
        name, f = hooks[k % 3]
        if k // 3 & 1:
            delattr(Sub, name)
        else:
            setattr(Sub, name, f)
        k += 1


N = 20000
ok = bad = 0
t = threading.Thread(target=flip)
t.start()
try:
    n = Sub('x', ast.Load())
    for i in range(N):
        n.extra = i
        setattr(n, 'more', i)
        try:
            miss = n.nope
        except AttributeError:
            miss = None
        good = (n.id in ('x', 'hooked') and getattr(n, 'id') in ('x', 'hooked')
                and n.extra in (i, -i) and getattr(n, 'more') in (i, -i)
                and miss in (None, 'missing') and getattr(n, 'nope', None) in (None, 'missing'))
        if good:
            ok += 1
        else:
            bad += 1
finally:
    stop.set()
    t.join()
for name, f in hooks:
    if name in Sub.__dict__:
        delattr(Sub, name)
n = Sub('x', ast.Load())
n.extra = 5
print(ok == N, bad, n.id, n.extra, getattr(n, 'nope', 'default'))
