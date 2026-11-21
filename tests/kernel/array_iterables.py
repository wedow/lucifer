from array import array


def show(call, *args):
    try:
        print(call(*args))
    except Exception as exc:
        print(type(exc).__name__, str(exc))


class Source:
    def __iter__(self):
        print('iter source')
        return Cursor()


class Cursor:
    def __iter__(self):
        print('iter cursor')
        return self

    def __next__(self):
        print('next cursor')
        raise StopIteration

    def __length_hint__(self):
        raise AssertionError('unexpected length hint')


class BrokenSource:
    def __iter__(self):
        raise TypeError('cannot start iteration')


for code in ('H', 'Z'):
    for source in (5, Source(), BrokenSource(), [1], (1,), b'a', 'a', array('w', 'a')):
        print('construct', code, type(source).__name__)
        show(array, code, source)
show(array, 'ZZ', Source())


class IndexedList(list):
    def __iter__(self):
        raise AssertionError('unexpected iteration')

    def __getitem__(self, index):
        print('get item', index)
        return super().__getitem__(index)


show(array, 'i', IndexedList([1, 2]))


def overflowing():
    print('yield good')
    yield 2
    print('yield overflow')
    yield -2**64
    print('yield extra')
    yield 3


def interrupted():
    print('yield before error')
    yield 4
    raise TypeError('iteration failed')


class ObservedIndex:
    def __index__(self):
        print('convert item')
        return 5


def converted():
    yield ObservedIndex()
    print('resume after conversion')


show(array, 'q', overflowing())
show(array, 'q', interrupted())
show(array, 'q', converted())
a = array('q', [1])
for source in (5, BrokenSource(), Source(), overflowing(), interrupted(), converted(), array('i', [9]), a):
    print('extend', type(source).__name__)
    show(a.extend, source)
    print(a)


class ChangingIndex:
    calls = 0

    def __index__(self):
        self.calls += 1
        return self.calls


a = array('q', [1])
show(a.extend, [ChangingIndex()])
print(a)
