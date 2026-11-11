# __new__ of a runtime type called for classes and types: a class of the
# same static base, a class of another static base, an exception class through
# its base exception's __new__, an extension-like inherited __new__, and the
# iterators the kernel makes by static type name while compiled code runs.

class MyList(list):
    pass


class MyZip(zip):
    pass


class MyErr(ValueError):
    pass


for f in (lambda: list.__new__(MyList), lambda: zip.__new__(MyZip, (1,), (2,)),
          lambda: Exception.__new__(MyErr), lambda: BaseException.__new__(MyErr),
          lambda: list.__new__(dict), lambda: dict.__new__(MyList), lambda: zip.__new__(map),
          lambda: int.__new__(bool)):
    try:
        r = f()
        print(type(r).__name__)
    except TypeError as e:
        print('TypeError', e)


def walk(n):
    total = 0
    for i in range(n):
        for a, b in zip((1, 2, 3), [4, 5, 6]):
            total += a * b
        total += sum(iter([i, 1])) + len(list(iter((i, 2)))) + len(list({i: 1}.items()))
        total += sum(1 for _ in MyZip((1, 2), (3, 4)))
    return total


print(walk(3000))
