# Delayed releases while full collections run. Replacing an instance's
# __dict__ releases the old dict only once lock-free readers are done (QSBR);
# the release frees the dict and its tuples to free lists, and every full
# collection empties all free lists with the world stopped and borrows ob_tid
# for its counts. Under -X lucifer_handoff the task moves between two carriers
# at every safepoint, so the carrier it left holds released dicts in its queue
# while the task collects on the other: those releases must wait for a task to
# be attached. The values are built by calls that run no Python loop, which
# would park at every iteration.
import gc


class Box:
    pass


boxes = [Box() for _ in range(8)]
pairs = dict.fromkeys(range(1000, 1300))
words = " ".join(map(str, range(300)))
total = 0
for i in range(200):
    for b in boxes:
        b.__dict__ = {"t": list(pairs.items()), "c": dict(pairs), "s": words.split(), "i": i}
    gc.collect()
    total += sum(len(b.t) + len(b.c) + len(b.s) + b.i for b in boxes)
print(total, len(gc.garbage))
