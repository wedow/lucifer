# Freeing a structure nested far deeper than the C stack allows.
import gc

for make in (lambda d: {1: d}, lambda d: [d], lambda d: (d,), lambda d: {d}, lambda d: frozenset([d])):
    d = None
    for i in range(300000):
        d = make(d)
    del d
    print('freed')


class Node:
    freed = 0

    def __init__(self, child):
        self.child = child

    def __del__(self):
        Node.freed += 1


node = None
for i in range(50000):
    node = Node(node)
del node
gc.collect()
print(Node.freed)
