# print: separators, terminators, file objects, flush and their errors.
class Sink:
    def __init__(self, name):
        self.name = name
        self.flushed = 0

    def write(self, s):
        print('write', self.name, repr(s))
        return len(s)

    def flush(self):
        self.flushed += 1
        print('flush', self.name)


def t(label, f, *a, **k):
    try:
        f(*a, **k)
    except BaseException as e:
        print(label, '!', type(e).__name__, e)


print()
print('a', 'b', 'c')
print('a', 'b', sep='')
print('a', 'b', sep='-', end='!\n')
print('a', 'b', sep=None, end=None)
print(1, 2.5, None, True, [1], (2,), {3: 4}, sep=', ')
print('x', end='')
print('')
print(*'abc', sep='|')
print('multi', 'sep', sep='<>')
print('héllo', '€', '\U0001F600')

s = Sink('s')
print('hello', 'world', file=s)
print('one', file=s, end='')
print(file=s)
print('f', 'g', sep='', end='', file=s, flush=True)
print('nf', file=s, flush=False)
print('none file', file=None)
print(1, 2, file=s, sep=None, end=None)
print('flush default', file=s, flush=None)


class Str:
    def __str__(self):
        return 'str!'

    def __repr__(self):
        return 'repr!'


print(Str(), [Str()], file=s)


class Bad:
    def __str__(self):
        raise ValueError('no str')


t('bad str', print, 'before', Bad(), file=s)
t('bad sep int', print, 1, 2, sep=1)
t('bad end int', print, 1, 2, end=1)
t('bad sep bytes', print, 1, sep=b' ')
t('bad end list', print, 1, end=[])
t('unknown kw', print, 1, foo=2)
t('bad str stdout', print, 'partial', Bad())


class NoWrite:
    pass


t('no write', print, 1, file=NoWrite())
t('file int', print, 1, file=3)
t('no flush', print, 'x', file=type('W', (), {'write': lambda self, s: None})(), flush=True)


class Raises:
    def write(self, s):
        raise OSError('disk full')


t('write raises', print, 'x', file=Raises())


class Counts:
    def __init__(self):
        self.n = 0

    def write(self, s):
        self.n += 1


c = Counts()
print('a', 'b', 'c', file=c)
print(c.n)
print(s.flushed)
