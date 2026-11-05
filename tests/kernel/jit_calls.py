# Calls the optimizing tier should inline once it has run: a method reached
# through a function that was compiled before the interpreter ever ran it
# (-X lucifer_jit=always compiles at the first call).
# Each result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that entries into compiled code do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Node:
    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


def visit(n):
    return n.get() + 1


def methods(n):
    node = Node(3)
    s = 0
    for i in range(n):
        s += visit(node)
    return s


print(methods(N))
