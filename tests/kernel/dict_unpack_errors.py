class BrokenKeys:
    def keys(self):
        raise AttributeError('keys failed')


class BrokenItem:
    def keys(self):
        return ['a']

    def __getitem__(self, key):
        print('getitem', key)
        raise KeyError(('missing', 1))


class AttributeItem(BrokenItem):
    def __getitem__(self, key):
        raise AttributeError('getitem failed')


class MultipleErrorArgs(BrokenItem):
    def __getitem__(self, key):
        raise KeyError('first', 'second')


class CustomKeyError(KeyError):
    def __getattribute__(self, name):
        if name == 'args':
            return ()
        return super().__getattribute__(name)

    def __str__(self):
        return 'custom key error'


class CustomItem(BrokenItem):
    def __getitem__(self, key):
        raise CustomKeyError('missing')


class IteratedKeys:
    def keys(self):
        return self

    def __iter__(self):
        yield 'a'
        raise AttributeError('key iteration failed')


class TracedMapping:
    @property
    def keys(self):
        print('look up keys')
        return lambda: [1]

    def __getitem__(self, key):
        print('read non-string key', key)
        return 2


def f(**kwargs):
    return kwargs


def call(mapping):
    return f(**mapping)


def duplicate(mapping):
    return f(a=1, **mapping)


def display(mapping):
    return {**mapping}


for operation in (call, duplicate, display):
    print(operation.__name__)
    for mapping in (BrokenKeys(), BrokenItem(), AttributeItem(),
                    MultipleErrorArgs(), CustomItem(), IteratedKeys()):
        try:
            operation(mapping)
        except Exception as exc:
            print(type(exc).__name__, exc, 'context', exc.__context__)

try:
    f(**TracedMapping(), **BrokenKeys())
except TypeError as exc:
    print(type(exc).__name__, exc)

long_type = type('M' + 'é' * 101, (BrokenKeys,), {})
for operation in (call, display):
    try:
        operation(long_type())
    except TypeError as exc:
        print('type name truncated', 'M' + 'é' * 99 in str(exc), 'é' * 100 in str(exc))

try:
    {}.update(BrokenKeys())
except Exception as exc:
    print(type(exc).__name__, exc)
