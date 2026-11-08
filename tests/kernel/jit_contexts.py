# with statements in hot code: a class's context manager, a lock, and a
# manager whose __exit__ swallows the exception raised in the body. Each
# result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that entries left to the interpreter and retired
# units do not grow with the iterations: LOAD_SPECIAL compiles.
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Managed:
    def __init__(self):
        self.depth = 0

    def __enter__(self):
        self.depth += 1
        return self

    def __exit__(self, typ, val, tb):
        self.depth -= 1
        return typ is KeyError


def contexts(n):
    m, lock = Managed(), threading.Lock()
    s = 0
    for i in range(n):
        with m as got:
            s += got.depth
        with lock:
            s += 1
        if i % 1000 == 0:
            with m:
                raise KeyError(i)
    return s, m.depth


def not_a_manager():
    try:
        with 5:
            pass
    except TypeError as e:
        return str(e)


print(contexts(N))
print(not_a_manager())
