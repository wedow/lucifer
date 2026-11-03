# Dropping the last reference to an object finalizes it at once, whichever
# carrier allocated it: the weakref callback has run before the next line.
import _weakref as weakref

count = 0


def callback(r):
    global count
    count += 1


class Node:
    pass


late = 0
for n in range(200):
    x = Node()
    r = weakref.ref(x, callback)
    for i in range(n % 4):
        id(r)
    if n % 3:
        id(r)
    before = count
    del x
    after = count
    if after is before:
        late += 1
print("finalized late:", late)
print("finalized:", count)
