# Argument shapes: keywords, counts, missing and extra arguments.
def attempt(f):
    try:
        print(repr(f()))
    except Exception as e:
        print(type(e).__name__, e)


b = b'a,b c'
ba = bytearray(b)
for f in (
    lambda: b.split(sep=b','), lambda: b.split(maxsplit=1), lambda: b.split(b',', maxsplit=1), lambda: b.split(b',', sep=b','),
    lambda: b.split(x=1), lambda: b.rsplit(b',', 1, 2), lambda: b.splitlines(keepends=True), lambda: b.splitlines(x=1),
    lambda: b.splitlines(True, True), lambda: b.expandtabs(tabsize=2), lambda: b.expandtabs(1, 2), lambda: b.expandtabs(x=2),
    lambda: b.decode('utf-8', 'strict', 1), lambda: b.decode(encoding='utf-8'), lambda: b.decode(x=1), lambda: b.decode('utf-8', encoding='x'),
    lambda: b.hex(sep=b','), lambda: b.hex(bytes_per_sep=2), lambda: b.hex(b',', 2, 3), lambda: b.hex(x=1), lambda: b.hex(sep=''), lambda: b.hex('é'),
    lambda: b.replace(b'a', b'b', count=1), lambda: b.replace(b'a', b'b', 1, count=1), lambda: b.replace(b'a'), lambda: b.replace(b'a', b'b', 1, 2),
    lambda: b.replace(b'a', b'b', x=1), lambda: b.replace(b'a', b'b', 'x'),
    lambda: b.join(), lambda: b.join(1, 2), lambda: b.partition(), lambda: b.partition(b',', 1), lambda: b.partition(sep=b','),
    lambda: b.translate(), lambda: b.translate(None, delete=b'a'), lambda: b.translate(None, b'a', 1), lambda: b.translate(None, x=1),
    lambda: b.strip(b'a', b'b'), lambda: b.strip(chars=b'a'), lambda: b.lstrip(1), lambda: b.zfill(), lambda: b.zfill(1, 2), lambda: b.zfill(width=2),
    lambda: b.center(), lambda: b.center(1, 2, 3), lambda: b.center(width=3), lambda: b.ljust(3, 4), lambda: b.rjust(3, b'ab'),
    lambda: b.removeprefix(), lambda: b.removeprefix(b'a', b'b'), lambda: b.removeprefix(prefix=b'a'), lambda: b.removesuffix(1),
    lambda: b.count(b'a', 1, 2, 3), lambda: b.count(b'a', start=1), lambda: b.index(b'a', 1, 2, 3), lambda: b.startswith(b'a', 1, 2, 3),
    lambda: b.startswith(), lambda: b.endswith(b'a', 'x'), lambda: b.find(b'a', None, None), lambda: b.find(b'a', 1.5),
    lambda: bytes(source=b'x'), lambda: bytes(b'x', source=b'y'), lambda: bytes('x', encoding='utf-8'), lambda: bytes('x', 'utf-8', errors='strict'),
    lambda: bytes(x=1), lambda: bytes(1, 2, 3, 4), lambda: bytes('é', 'ascii'), lambda: bytes('é', 'nope'), lambda: bytes('é', 'ascii', 'ignore'),
    lambda: bytearray(source=b'x'), lambda: bytearray('x', encoding='utf-8'), lambda: bytearray(x=1), lambda: bytearray(1, 2, 3, 4), lambda: bytearray('é', 'ascii'),
    lambda: bytearray(b'x', 'ascii'), lambda: bytearray(3, errors='x'), lambda: bytearray(encoding='x'), lambda: bytearray(errors='x'),
    lambda: ba.append(), lambda: ba.append(1, 2), lambda: ba.append(item=1), lambda: ba.insert(1), lambda: ba.insert(1, 2, 3), lambda: ba.pop(1, 2),
    lambda: ba.pop(index=0), lambda: ba.pop('x'), lambda: ba.remove(), lambda: ba.extend(), lambda: ba.extend(iterable_of_ints=[1]), lambda: ba.clear(1),
    lambda: ba.copy(1), lambda: ba.reverse(1), lambda: ba.resize(), lambda: ba.resize(1.5), lambda: ba.resize(size=1), lambda: ba.resize(2 ** 70),
    lambda: bytes.fromhex(), lambda: bytes.fromhex('a', 'b'), lambda: bytes.fromhex(string='aa'), lambda: bytes.fromhex(b'aa bb'), lambda: bytes.fromhex(bytearray(b'cc')),
    lambda: bytes.fromhex('a  b'), lambda: bytes.fromhex('aa b'), lambda: bytes.fromhex('é'), lambda: bytes.fromhex(' '), lambda: bytes.fromhex(''),
    lambda: bytes.maketrans(b'a'), lambda: bytes.maketrans(b'a', b'b', b'c'), lambda: bytes.maketrans('a', 'b'), lambda: b.maketrans(b'ab', b'cd')[97:99],
    lambda: bytes.__new__(), lambda: bytes.__new__(1), lambda: bytearray.__new__(int), lambda: bytes.__new__(bytes, b'x'), lambda: bytearray.__init__(),
    lambda: b.__getitem__(), lambda: b.__len__(1), lambda: b.__contains__(), lambda: b.__add__(b'x'), lambda: b.__mul__(2), lambda: b.__rmul__(2), lambda: b.__mod__(()),
    lambda: b.__eq__(b), lambda: b.__lt__(1), lambda: b.__bytes__(), lambda: b.__bytes__(1), lambda: b.__getnewargs__(),
    lambda: memoryview(b, 1), lambda: memoryview(object=b), lambda: memoryview(x=b), lambda: memoryview(b, object=b), lambda: memoryview(b).tobytes(order='F'),
    lambda: memoryview(b).tobytes('A'), lambda: memoryview(b).tobytes(1), lambda: memoryview(b).hex(x=1), lambda: memoryview(b).cast(), lambda: memoryview(b).cast(1),
    lambda: memoryview(b).cast('B', shape=[5]), lambda: memoryview(b).cast('B', shape=[2, 3]), lambda: memoryview(b).tolist(1), lambda: memoryview(b).release(1),
    lambda: memoryview(b).count(), lambda: memoryview(b).index(1), lambda: memoryview(b).index(44), lambda: memoryview(b).count(44),
):
    attempt(f)
