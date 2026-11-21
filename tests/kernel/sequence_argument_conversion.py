import sys


def show(label, method, *args):
    try:
        print(label, method(*args))
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))


class Index:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


class EmptyLength(bytes):
    def __len__(self):
        return 0


class NegativeLength(bytes):
    def __len__(self):
        return -1


class OneLength(bytes):
    def __len__(self):
        return 1


class OneTextLength(str):
    def __len__(self):
        return 1


big = sys.maxsize + 1
for data in (b'ab', bytearray(b'ab')):
    print(type(data).__name__)
    show('center', data.center, big, b'x')
    show('ljust', data.ljust, Index(big), memoryview(b'*'))
    show('rjust', data.rjust, big, b'x')
    show('zfill', data.zfill, -big - 1)
    show('split', data.split, b'b', Index(big))
    show('rsplit', data.rsplit, None, big)
    show('count bounds', data.count, -1, b'\0', 3)
    show('find bounds', data.find, -1, 0, b'\0')
    show('count clamped', data.count, b'a', -big - 1, big)
    show('ljust bad fill', data.ljust, 8, memoryview(b'*'))
    show('rjust bytes subclass', data.rjust, 8, EmptyLength(b'x'))
    show('expandtabs upper overflow', data.expandtabs, 2**31)
    show('expandtabs lower overflow', data.expandtabs, Index(-2**31 - 1))
    show('hex negative length', data.hex, NegativeLength(b':'), -2)
    show('hex length override', data.hex, OneLength(b'::'))
    show('hex empty storage', data.hex, OneLength(b''))
    show('hex wide storage', data.hex, OneTextLength(':Ā'))
    show('hex unsupported separator', data.hex, memoryview(b':'))
    show('hex group first', data.hex, NegativeLength(b':'), 2**31)
    show('hex min group', data.hex, b':', -2**31)

def subscript(seq, key):
    return seq[key]


for seq in ([1], (1,)):
    show('index overflow', subscript, seq, Index(big * 2))
    show('negative index overflow', seq.__getitem__, Index(-big - 1))
    show('valid index', seq.__getitem__, Index(-1))
    show('invalid index type', seq.__getitem__, type('é' * 99 + '界' * 102, (), {})())


class Hinted:
    def __iter__(self):
        print('iter')
        return iter((2,))

    def __length_hint__(self):
        print('hint')
        return self.hint


def inplace_extend(values, iterable):
    values += iterable


hinted = Hinted()
values = [1]
for hint in (1, -1, 'bad', NotImplemented, 2**60, sys.maxsize, big):
    hinted.hint = hint
    show('in-place extend', inplace_extend, values, hinted)
    print(values)
