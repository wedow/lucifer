"""Array items read and stored in hot loops while other threads resize the
array (bytearray's extend and slice deletes: the storage moves) and export
it (a resize while a view is held raises BufferError). Every read and
store sees one size and one storage: an index is in range or raises
IndexError, nothing reads or writes storage a resize freed, and the items
stay the typecode's. The same races run on a bytearray.
CPython 3.14t's array takes no lock, so racing one is undefined there: the
arrays race only where array is a bytearray class (whose storage has the
bytearray's lock), and print the same line either way."""
import sys
import threading
from array import array

# A collection after every allocation (-X lucifer_stress) leaves time for far fewer rounds.
STRESS = 'lucifer_stress' in sys._xoptions
ROUNDS = 30 if STRESS else 3000
errors = []


def run(*bodies):
    threads = [threading.Thread(target=b) for b in bodies]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def reader(a, rounds):
    def body():
        s = 0
        for r in range(rounds):
            for i in range(64):
                try:
                    s += a[i] + a[-1 - i]
                except IndexError:
                    pass
                except BaseException as e:
                    errors.append(type(e).__name__)
                    return
    return body


def writer(a, rounds, value):
    def body():
        for r in range(rounds):
            for i in range(64):
                try:
                    a[i] = value
                    a[-1 - i] = value
                except IndexError:
                    pass
                except BaseException as e:
                    errors.append(type(e).__name__)
                    return
    return body


def resizer(a, rounds, item):
    """Moves the storage through bytearray's own methods, whole items at a time."""
    one = bytes(array(a.typecode, [item])) if isinstance(a, array) else bytes([item])
    size = len(one)

    def body():
        for r in range(rounds):
            try:
                bytearray.extend(a, one * (r % 70 + 1))
                n = bytearray.__len__(a)
                if n > 20 * size:
                    bytearray.__delitem__(a, slice(3 * size, n - 3 * size))
                bytearray.__delitem__(a, slice(-size, None))
            except BufferError:
                pass
            except BaseException as e:
                errors.append(type(e).__name__)
                return
    return body


def exporter(a, rounds):
    def body():
        for r in range(rounds):
            try:
                with memoryview(a) as m:
                    m.nbytes
            except BaseException as e:
                errors.append(type(e).__name__)
                return
    return body


def race(a, item, value):
    run(reader(a, ROUNDS), reader(a, ROUNDS), writer(a, ROUNDS, value), resizer(a, ROUNDS, item),
        resizer(a, ROUNDS, item), exporter(a, ROUNDS))
    return errors[:3], all(x == item or x == value for x in a)


for code, item, value in (('d', 1.5, -2.25), ('i', 7, -9), ('B', 3, 200), ('q', 2**40, -2**50), ('f', 0.5, 4.0)):
    print(code, *(race(array(code, [item] * 32), item, value) if issubclass(array, bytearray) else ([], True)))
print('bytearray', *race(bytearray([3] * 32), 3, 200))
