"""Instance attributes under concurrent mutation: threads store, read and
delete their own names on shared objects while another thread moves the
objects' attributes into dicts (vars, __dict__, __class__ assignment) and
replaces objects. Each thread must read back what it wrote, and no store
may be lost or resurrected."""
import threading

N = 32
ROUNDS = 20000


class A:
    def __init__(self):
        self.a = 0
        self.b = 0


class B:
    def __init__(self):
        self.a = 0
        self.b = 0
        self.c = 0


objs = [A() for _ in range(N)]
errors = []
start = threading.Barrier(5)


def worker(tid):
    name = 'w' + str(tid)
    bad = 0
    start.wait()
    for i in range(ROUNDS):
        o = objs[i % N]
        setattr(o, name, i)
        o.a = i
        o.b = tid
        if getattr(o, name) != i:
            bad += 1
        if i % 3 == 0:
            delattr(o, name)
            if hasattr(o, name):
                bad += 1
    errors.append(bad)


def mover():
    start.wait()
    for i in range(ROUNDS // 4):
        k = i % N
        o = objs[k]
        if i % 4 == 0:
            vars(o)
        elif i % 4 == 1:
            o.__class__ = B if type(o) is A else A
        elif i % 4 == 2:
            len(o.__dict__)
        else:
            objs[k] = A()


threads = [threading.Thread(target=worker, args=(t,)) for t in range(4)]
threads.append(threading.Thread(target=mover))
for t in threads:
    t.start()
for t in threads:
    t.join()
print('lost or resurrected:', sum(errors))
print('a is an int everywhere:', all(type(o.a) is int for o in objs))
