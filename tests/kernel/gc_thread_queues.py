# Objects a thread owns, whose last reference another thread drops, are
# queued to their owner; gc.collect() merges every thread's queue first, so
# they are freed when it returns, the owner still running.
import gc
import threading
import weakref

gc.disable()
N = 20
freed = []
box = []
made = threading.Event()
done = threading.Event()


class Obj:
    pass


def owner():
    for _ in range(N):
        o = Obj()
        weakref.finalize(o, freed.append, 1)
        box.append(o)
        del o
    made.set()
    done.wait()


t = threading.Thread(target=owner)
t.start()
made.wait()
print("before:", len(box), len(freed))
box.clear()          # drops references the owner counted: queued to the owner
gc.collect()
print("after:", len(freed))
done.set()
t.join()
