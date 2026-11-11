# Fields of builtin objects read by some threads while another rewrites them:
# a property re-initialized under readers that go through it, and a
# classmethod's function read while its __init__ runs again. Every read sees
# one of the values written, whole.
import threading

N = 4000
values = [lambda self, i=i: i for i in range(8)]


class C:
    p = property(values[0])


cm = classmethod(values[0])
seen = set()
bad = []


def reader():
    c = C()
    local = set()
    for _ in range(N):
        v = c.p
        f = cm.__func__
        if not (0 <= v < 8) or f not in values:
            bad.append((v, f))
        local.add(v)
    seen.update(local)


def writer():
    for i in range(N):
        f = values[i % 8]
        C.p.__init__(f)
        cm.__init__(f)


threads = [threading.Thread(target=reader) for _ in range(3)] + [threading.Thread(target=writer)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print('bad reads', bad)
print('values seen within range', seen <= set(range(8)))
