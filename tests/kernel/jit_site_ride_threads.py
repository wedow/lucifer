# luc-szyb: threads drive one function whose place keeps meeting new classes
# (learned rebuilds, marks, and the sites that ride them) while another
# thread drops the classes' namespaces and collects. The rides walk the
# code's units under its profile lock, so no walk parks while a unit it
# reaches is killed and freed. Output: the sums, which do not depend on the
# schedule.
import gc
import sys
import threading

n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
ROUNDS = 12


def hot(x):
    return (x * 7 + 3) & 255


def work(o, x, k):
    s = o.v(x)
    if k:
        s += hot(x + 1) + hot(x + 2) + hot(x + 3) + hot(x + 4) + hot(x + 5) + hot(x + 6)
    return s + hot(x)


def make(r):
    ns = {}
    exec(f"class K{r}:\n    def v(self, x): return x + {r}\n", ns)
    return ns[f"K{r}"]


def driver(t, out):
    s = 0
    for r in range(ROUNDS):
        o = make(r * 10 + t)()
        for i in range(n // ROUNDS):
            s += work(o, i, i % 13 == 1)
        del o
    out[t] = s


def dropper(stop):
    while not stop.is_set():
        for _ in range(4):
            make(999)
        gc.collect()


out = [0] * 3
stop = threading.Event()
d = threading.Thread(target=dropper, args=(stop,))
d.start()
ts = [threading.Thread(target=driver, args=(t, out)) for t in range(3)]
for th in ts:
    th.start()
for th in ts:
    th.join()
stop.set()
d.join()
print(out)
