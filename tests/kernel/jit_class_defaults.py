# An instance attribute with a class value behind it: read in compiled code
# while the instance has none of its own (the class's), has one, loses it
# again, and while the class's value changes or its values move to a dict.
import sys


class Stream:
    closed = False
    buffer = None
    count = 0
    name = 'stream'
    items = []          # not immortal: read the general way

    def __init__(self, n):
        self.n = n

    def state(self):
        return (self.closed, self.buffer, self.count, self.name, len(self.items), self.n)

    def method(self):
        return 'method'


def run(objects, rounds):
    seen = {}
    for _ in range(rounds):
        for o in objects:
            s = o.state()
            seen[s] = seen.get(s, 0) + 1
    return sorted(seen.items(), key=repr)


rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 300
a, b, c = Stream(1), Stream(2), Stream(3)
print(run([a, b], rounds))
b.closed = True
b.count = b.count + 5
c.buffer = b'xy'
print(run([a, b, c], rounds))
del b.closed
c.name = 'own'
print(run([a, b, c], rounds))
Stream.closed = 'class changed'
Stream.count = 7
print(run([a, b, c], rounds))
a.__dict__['extra'] = 1     # a's values move to a dict
vars(c).clear()
c.n = 30
print(run([a, b, c], rounds))
Stream.method = lambda self: 'replaced'
a.method = lambda: 'own method'
print(a.method(), b.method(), Stream.items is c.items)
