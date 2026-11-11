"""A class default (a plain class value) read through instances that have
not set their own, have set it, set it again and deleted it, and one whose
values moved to its dict, at one site and at a site of several classes (an
io-like base whose `closed` property reads a name-mangled default); and a
property whose getter is too big to inline, called in a hot loop."""
from support import loops


class Base:
    __closed = False
    flag = 'class'

    @property
    def closed(self):
        return self.__closed

    def close(self):
        self.__closed = True

    def read(self):
        return 'closed' if self.closed else self.flag


class A(Base):
    def __init__(self):
        self.a = 1


class B(Base):
    def __init__(self):
        self.b = 2


class Big:
    def __init__(self, n):
        self.n = n

    @property
    def total(self):
        s = 0
        for i in range(self.n):
            s += i * i if i % 3 else -i
            s ^= i << 2
        return s


def run():
    xs = [A(), B(), A(), B()]
    out = []
    for i in range(400):
        x = xs[i % 4]
        out.append(x.read())
        if i == 100:
            xs[1].close()
        if i == 200:
            xs[2].flag = 'own'
        if i == 300:
            del xs[2].flag
            vars(xs[3])
            xs[3].close()
    big = Big(20)
    s = 0
    for i in range(200):
        s += big.total
    return out[95:110], out[195:205], out[295:310], out[-4:], s


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
