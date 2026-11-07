# A writer rebinds a global and a class attribute while another thread
# compiles a loop that relies on their values. The compile either reads the
# new values, or is not published, or is published before the stores and
# is taken back by them: the iteration after the reader sees the writer's
# flag sees the new values. Each round compiles a fresh code object, and
# the writer stores at a different moment of the compile.
import gc
import threading
import types

gc.disable()


def a():
    return 'a'


def b():
    return 'b'


target = a


class Holder:
    v = 'a'

ROUNDS = 400


class State:
    def __init__(self):
        self.go = -1            # the round the reader has begun
        self.flipped = -1       # the round whose store is made


def body(state, r):
    seen = -1
    while True:
        v = target(), Holder.v
        if seen == r:
            return v
        seen = state.flipped


def reader(state, out):
    for r in range(ROUNDS):
        f = types.FunctionType(body.__code__.replace(co_name=f"body{r}"), globals())
        state.go = r
        out.append(f(state, r))


def writer(state):
    global target
    for r in range(ROUNDS):
        while state.go != r:
            pass
        for _ in range(r * 37 % 10000):
            pass
        target = b if r % 2 else a
        Holder.v = 'b' if r % 2 else 'a'
        state.flipped = r


state = State()
out = []
threads = [threading.Thread(target=reader, args=(state, out)), threading.Thread(target=writer, args=(state,))]
for t in threads:
    t.start()
for t in threads:
    t.join()
wrong = [r for r, v in enumerate(out) if v != (('b', 'b') if r % 2 else ('a', 'a'))]
print(len(out), wrong[:10])
