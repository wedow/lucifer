# One thread's compiled code stores floats unboxed into an attribute while
# another stores float objects into it and reads it back: every reader must
# see an object (never the unboxed marker), the same one each time.
import threading


class P:
    def __init__(self):
        self.a = 0.0


def bump(p, n):
    s = 0.0
    for i in range(n):
        s += p.a * 1.0
        p.a = p.a + 1.0
    return s


bad = 0
for trial in range(300):
    p = P()
    bump(p, 2000)

    def other():
        for _ in range(2000):
            p.a = float(p.a)

    t = threading.Thread(target=other)
    t.start()
    bump(p, 5000)
    t.join()
    if not (p.a == p.a and p.a is p.a):
        bad += 1
print('bad', bad)
