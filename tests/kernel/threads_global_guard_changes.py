"""Readers load a global and a builtin in hot loops while another thread
deletes and re-adds the global, shadows the builtin with a global and
unshadows it, grows the module dict past resizes, and replaces the builtin.
Every load must give a value one of those states holds, or NameError while
the global is deleted."""
import builtins
import threading

READS = 300000
value = 1
saved_abs = builtins.abs
stop = threading.Event()


def churn():
    global value
    k = 0
    while not stop.is_set():
        del value
        value = 1
        globals()['abs'] = lambda x: 2
        del globals()['abs']
        builtins.abs = lambda x: 2
        builtins.abs = saved_abs
        for _ in range(40):
            globals()['grown_%d' % k] = k
            k += 1


def read(out):
    bad = missing = 0
    for _ in range(READS):
        try:
            if value != 1:
                bad += 1
        except NameError:
            missing += 1
        if abs(-2) != 2:
            bad += 1
    out.append(bad)


out = []
writer = threading.Thread(target=churn)
readers = [threading.Thread(target=read, args=(out,)) for _ in range(3)]
writer.start()
for t in readers:
    t.start()
for t in readers:
    t.join()
stop.set()
writer.join()
print('bad loads', sum(out))
