# Compiled code stays correct while another thread changes what it relies
# on: a thread runs a hot loop over a global, a method and an attribute;
# once it is warm, the main thread rebinds the global, replaces the method
# and swaps the attribute's type. The loop must see each change and stop.
import threading

LIMIT = 1


class Box:
    def __init__(self):
        self.v = 1

    def get(self):
        return 1


def spin(box, warm, out):
    seen = 0
    total = 0
    i = 0
    while seen != 3:
        i += 1
        if i == 20000:
            warm.set()
        a = LIMIT
        b = box.get()
        c = box.v
        total += a + b
        seen = (a == 2) + (b == 2) + (c == 2.5)
    out.append((a, b, c, total > 0))


def main():
    box = Box()
    warm = threading.Event()
    out = []
    t = threading.Thread(target=spin, args=(box, warm, out))
    t.start()
    warm.wait()
    global LIMIT
    LIMIT = 2
    Box.get = lambda self: 2
    box.v = 2.5
    t.join()
    print(out)


main()


# Two threads run the same compiled function on their own data while a third
# keeps redefining a helper it calls.
def helper(x):
    return x + 1


def work(n, res, idx):
    acc = 0
    for i in range(n):
        acc += helper(i) - i
    res[idx] = acc


res = [0, 0]
stop = threading.Event()


def churn():
    global helper
    while not stop.is_set():
        helper = lambda x: x + 1
        stop.wait(0.0005)


c = threading.Thread(target=churn)
c.start()
ts = [threading.Thread(target=work, args=(200000, res, k)) for k in range(2)]
for t in ts:
    t.start()
for t in ts:
    t.join()
stop.set()
c.join()
print(res)
