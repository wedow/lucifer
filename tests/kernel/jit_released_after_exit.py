# Compiled code releases what an instruction stops naming, finding what the
# state still names from a walk it keeps between instructions. An exit
# whose state differs from the frame's walks the whole state instead and
# sets the kept walk aside; the walk kept next must not answer for values
# an older one found, or a value the state no longer names is never
# released. Here an __init__ inlined into a recursive build leaves a new
# list unreleased after such an exit: every tree must be freed once the
# builds return.
import gc

alive = 0


class Tree:
    def __init__(self, depth):
        global alive
        alive += 1
        self.kids = [Tree(depth - 1), Tree(depth - 1)] if depth > 0 else []

    def size(self):
        return 1 + sum(k.size() for k in self.kids)

    def __del__(self):
        global alive
        alive -= 1


def trees(n):
    return sum(Tree(i % 5).size() for i in range(n))


print('nodes', trees(300))
gc.collect()
print('trees left', alive)
