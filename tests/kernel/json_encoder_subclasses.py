import json


class BrokenList(list):
    def __len__(self):
        return 1

    def __iter__(self):
        raise ValueError('iteration failed')


class CustomList(list):
    def __len__(self):
        return 0

    def __iter__(self):
        return iter([1])


class EmptyList(list):
    def __iter__(self):
        return iter(())


class CustomTuple(tuple):
    def __iter__(self):
        yield object()


class NonIterableList(list):
    __iter__ = None


class RecursiveList(list):
    def __iter__(self):
        yield self


class CustomItems(dict):
    def items(self):
        return self.result


class BrokenItems(dict):
    def items(self):
        raise TypeError('items failed')


class BrokenNext:
    def __iter__(self):
        return self

    def __next__(self):
        raise TypeError('next failed')


def show(value, **kwargs):
    try:
        print(repr(json.dumps(value, **kwargs)))
    except Exception as exc:
        print(type(exc).__name__, str(exc), getattr(exc, '__notes__', None))


for indent in (None, 1):
    for value in (BrokenList(), CustomList([9]), EmptyList([9]), CustomTuple(),
                  NonIterableList([9]), RecursiveList()):
        show(value, indent=indent)

    mapping = CustomItems(stored=0)
    for sort_keys in (False, True):
        show(BrokenItems(stored=0), indent=indent, sort_keys=sort_keys)
        for result in (5, [('b', 1), ('a', 2)], iter((('b', 1), ('a', 2))), BrokenNext()):
            mapping.result = result
            show(mapping, indent=indent, sort_keys=sort_keys)
            if type(result) is list:
                print(result)
