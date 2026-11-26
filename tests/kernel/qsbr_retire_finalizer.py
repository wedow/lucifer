# Delayed releases run finalizers only at a poll. Replacing an instance's
# __dict__ queues the old dict for release once lock-free readers are done;
# every thread here also grows and clears a shared list, whose resize queues
# the old array under the list's lock. A queue that grew long was processed by
# the retire itself, so the old dicts' finalizers ran inside the resize, and
# the list was changed under the change in progress (a crash in a later
# clear). The cycles keep the collector freeing alongside.
#
# Each finalizer appends to a list of the thread it runs on, which only that
# thread changes: Lucifer retires its old array on a resize all the same,
# while CPython frees an unshared list's array at once. With one list for
# every finalizer, CPython 3.14t deadlocks now and then: it too runs
# finalizers inside a shared list's resize, and one that appends to that
# list waits on the lock its own thread holds.
import gc
import threading

local = threading.local()


class Mortal:
    def __del__(self, local=local):
        try:
            died = local.died
        except AttributeError:
            died = local.died = []
        died.append(1)


class Box:
    pass


boxes = [Box() for _ in range(4)]
shared = []


def work(k):
    for i in range(400):
        boxes[i % 4].__dict__ = {"m": Mortal(), "l": [i] * (i % 7)}
        cycle = {"m": Mortal()}
        cycle["c"] = cycle
        shared.append(object())
        if len(shared) > 64:
            shared.clear()
        if i % 97 == k % 97:
            gc.collect()


threads = [threading.Thread(target=work, args=(k,)) for k in range(48)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(len(shared) <= 64)
