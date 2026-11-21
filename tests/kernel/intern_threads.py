"""Equal strs interned from many threads at once yield one object: the
interned table takes its lock for a miss, for runtime strs and for the code
image's alike."""
import sys
import threading

N, T = 2000, 8
barrier = threading.Barrier(T)
results = [None] * T


def work(k):
    barrier.wait()
    fresh = [sys.intern("".join(["zq", str(i), "_w"])) for i in range(N)]
    image = [sys.intern("".join(list(n))) for n in ("__init__", "path_hooks", "isabs", "splitext")]
    results[k] = fresh + image


threads = [threading.Thread(target=work, args=(k,)) for k in range(T)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(sum(1 for i in range(N + 4) if any(r[i] is not results[0][i] for r in results)))
print(results[0][N] is object.__init__.__name__, sys.intern("zq7_w") is results[3][7])
