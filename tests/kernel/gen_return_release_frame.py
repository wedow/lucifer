"""A generator that returns releases its locals after its frame leaves the
stack (GH-99729): a finalizer one of them runs sees its consumer on top, and
never a frame whose locals are being released (luc-kfmv)."""
import gc
import sys

kept = []


class Probe:
    def __del__(self):
        f = sys._getframe(1)
        if f.f_code.co_name == "gen":
            kept.append(dict(f.f_locals))


def gen(n):
    q = [str(i) * 3 for i in range(n % 7 + 1)]
    p = Probe()
    r = {"k": [n]}
    yield n
    return 1


def consume(n):
    for v in gen(n):
        pass
    return v


for i in range(4000):
    consume(i)
    if kept:
        junk = [bytearray(64) for _ in range(50)]
gc.collect()
bad = 0
for d in kept:
    for k, v in d.items():
        try:
            repr(v)
        except Exception:
            bad += 1
print(len(kept), bad)
