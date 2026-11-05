class Spy:
    def __init__(self, **kw):
        self.d = dict(kw)
        self.log = []

    def __getitem__(self, key):
        self.log.append(('get', key))
        return self.d[key]

    def __setitem__(self, key, value):
        self.log.append(('set', key))
        self.d[key] = value

    def __delitem__(self, key):
        self.log.append(('del', key))
        del self.d[key]


m = Spy(x=2, y=3)
print(eval('x * y + len("ab")', {'len': len}, m), m.log)
exec('z = x + y\ndel x\nw = [i for i in range(z)]', {}, m)
print(sorted(m.d), m.d['z'], m.log)
exec('def f():\n    return z\nclass C:\n    a = z\nv = f', {'z': 9}, m)
print(m.d['v'](), m.d['C'].a)
try:
    eval('missing', {}, m)
except NameError as e:
    print(type(e).__name__, e)
try:
    eval('1', {}, 5)
except TypeError as e:
    print(e)
print(eval('abs(q)', {}, {'q': -1}))
