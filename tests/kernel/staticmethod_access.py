# A staticmethod reached through an instance or its class answers its
# callable, as staticmethod.__get__ does, whichever path asks: the
# interpreter's generic attribute load, compiled code, getattr. A subclass's
# own __get__, an instance attribute that shadows it, a staticmethod
# initialized again and a class attribute replaced later all decide as on
# CPython.
N = 3000


def helper(a, b):
    return a + b


class Node:
    @staticmethod
    def append(x, xs):
        return xs + [x]

    sm = staticmethod(helper)


class Loud(staticmethod):
    def __get__(self, obj, owner=None):
        return lambda *a: ('loud', a)


class Other:
    append = Loud(helper)


def run(n, o):
    out = None
    for i in range(n):
        out = o.append(i, [])
    return out


node = Node()
print(run(N, node), run(N, Node), run(10, Other()))
print(node.sm is helper, Node.sm is helper, getattr(node, 'sm') is helper, Node.__dict__['sm'].__func__ is helper)
node.append = lambda x, xs: 'shadowed'
print(run(N, node))
del node.append
sm = Node.__dict__['sm']
sm.__init__(lambda a, b: a * b)
print(node.sm(3, 4), Node.sm(3, 4))
Node.append = staticmethod(lambda x, xs: 'replaced')
print(run(N, node))
