# Compiled code on a worker thread calling functions and methods the main
# thread defined (a server's handlers): their function and code objects are
# owned by the main thread. tools/jitcheck runs this at two sizes and
# requires that the frames whose end goes to C (-X lucifer_jit_stats,
# "frames ended in C") do not grow with the calls.
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def add(a, b):
    return a + b


def rec(n, b):
    # Recursion stays a call (the inliner refuses it): a direct entry.
    return rec(n - 1, b) + 1 if n else b


class Acc:
    def __init__(self):
        self.t = 0

    def put(self, x):
        self.t = add(self.t, x)
        return self.rec(2)

    def rec(self, n):
        return self.rec(n - 1) if n else self.t


def work(out):
    acc = Acc()
    s = 0
    for i in range(N):
        s = add(s, i & 7) + rec(3, 0)
        acc.put(i & 3)
    out.append((s, acc.t))


out = []
t = threading.Thread(target=work, args=(out,))
t.start()
t.join()
print(out)
