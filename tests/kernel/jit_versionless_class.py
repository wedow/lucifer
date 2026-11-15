"""A class changed more often than it can be given versions keeps no
version from then on. What the JIT knows of an instance's type is relied on
as of the class's version, so it knows nothing of this class's instances;
a type it learns from the profile must not be read back as known."""


class C:
    k = 0


o = C()
for i in range(2000):
    C.k = i
    o.k


def missing(o):
    try:
        o.n
    except AttributeError as e:
        return str(e)


for _ in range(5000):
    r = missing(o)
print(r)
print(C.k, o.k)
