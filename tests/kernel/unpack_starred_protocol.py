# Starred unpacking fills the prefix first, then extends a list from the
# iterator. That step calls iter() again and reads the first iterator's hint.
class Iterable:
    def __init__(self, iterator):
        self.iterator = iterator

    def __iter__(self):
        print('iterable iter')
        return self.iterator


class Iterator:
    def __init__(self, values, hint=NotImplemented, second_iter=None):
        self.values = iter(values)
        self.hint = hint
        self.second_iter = second_iter

    def __iter__(self):
        print('iterator iter')
        if isinstance(self.second_iter, Exception):
            raise self.second_iter
        if self.second_iter is not None:
            return self.second_iter
        return self

    def __next__(self):
        value = next(self.values)
        print('next', value)
        return value

    def __length_hint__(self):
        print('iterator hint')
        if isinstance(self.hint, Exception):
            raise self.hint
        return self.hint


def unpack(iterable):
    try:
        first, *middle, last = iterable
        print(first, middle, last)
    except Exception as e:
        print(type(e).__name__, e)


for hint in (KeyError('bad hint'), 'bad hint', -1, TypeError('no hint'), NotImplemented):
    unpack(Iterable(Iterator((1, 2, 3, 4), hint)))

unpack(Iterable(Iterator((1, 2), second_iter=ValueError('second iter'))))
unpack(Iterable(Iterator((1, 2), 3, iter((7, 8, 9)))))
unpack(Iterable(Iterator((), KeyError('prefix must come first'))))

# list() takes its hint from the original iterable.
print(list(Iterable(Iterator((1, 2), KeyError('unused hint')))))
