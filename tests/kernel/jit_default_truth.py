# The hot loop's truth tests fold for both classes. A subclass must pick up
# truth methods added to its base while compiled code is running.
import sys
from io import BytesIO


class Base:
    pass


class Child(Base):
    pass


def count(plain, stream, rounds):
    total = 0
    for _ in range(rounds):
        if plain:
            total += 1
        if stream:
            total += 2
    return total


def mutate(value, rounds):
    total = 0
    for i in range(rounds):
        if i == rounds // 4:
            Base.__len__ = lambda self: 0
        if i == rounds // 2:
            Base.__bool__ = lambda self: True
        if i == 3 * rounds // 4:
            del Base.__bool__
        if value:
            total += 1
    return total


rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
plain = Child()
stream = BytesIO()
print(mutate(plain, 3000))
Base.__bool__ = lambda self: 1
try:
    if plain:
        print('unexpected truth')
except TypeError as exc:
    print(type(exc).__name__, str(exc))
del Base.__bool__
del Base.__len__
print(count(plain, stream, rounds))
