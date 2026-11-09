# threading.local attributes: per-thread __dict__, class attributes,
# properties and methods of a subclass, deletion and the errors.
import threading

class L(threading.local):
    kind = 'cls'
    def __init__(self, n=0):
        self.n = n
    @property
    def double(self):
        return self.n * 2
    def bump(self):
        self.n += 1
        return self.n

loc = L(5)
out = [loc.n, loc.kind, loc.double, loc.bump(), sorted(loc.__dict__)]
loc.kind = 'inst'
out.append((loc.kind, L.kind))
del loc.kind
out.append(loc.kind)
for bad in ('missing',):
    try:
        getattr(loc, bad)
    except AttributeError as e:
        out.append(str(e))
try:
    del loc.missing
except AttributeError as e:
    out.append('del ' + str(e))
try:
    loc.double = 1
except AttributeError as e:
    out.append('property')
try:
    loc.__dict__ = {}
except AttributeError as e:
    out.append(str(e))
seen = []
def other():
    seen.append((loc.n, sorted(loc.__dict__), hasattr(loc, 'x')))
    loc.x = 1
t = threading.Thread(target=other); t.start(); t.join()
out.append((seen, hasattr(loc, 'x'), loc.n))
plain = threading.local()
plain.a = 1
t = threading.Thread(target=lambda: seen.append(hasattr(plain, 'a'))); t.start(); t.join()
out.append((plain.a, seen[-1]))
for i in range(300):
    plain.a += 1
out.append(plain.a)
print(out)
