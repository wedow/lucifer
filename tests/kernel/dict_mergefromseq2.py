from _testlimitedcapi import dict_mergefromseq2


class PairError(TypeError):
    def add_note(self, note):
        raise AssertionError('add_note override called')


class BadPair:
    def __iter__(self):
        yield 'key'
        exc = PairError('pair iteration failed')
        exc.__notes__ = ['original note']
        raise exc


def merge(pairs, override):
    target = {'existing': 0}
    try:
        dict_mergefromseq2(target, pairs, override)
    except (TypeError, ValueError) as exc:
        print(type(exc).__name__, str(exc), getattr(exc, '__notes__', []))
    print(target)


merge([7], 1)
merge([('existing', 1), ('added', 2), BadPair()], 0)
merge([('existing', 1), ('added', 2), (1, 2, 3)], 1)
