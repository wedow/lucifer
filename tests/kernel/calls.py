def show(x='-', y='-', z='-'):
    print(x, y, z)


def second(a, b):
    return b


def defaults(a, b='B', c='C'):
    print(a, b, c)


def kwonly(a, *, x, y='Y'):
    print(a, x, y)


def posonly(a, b, /, c):
    print(a, b, c)


def rest(a, *more):
    print(a, *more)


def options(a, **opts):
    show(a, **opts)


def everything(a, b='b', /, c='c', *args, d, e='e', **kw):
    print(a, b, c, *args)
    print(d, e)
    show(**kw)


def nothing():
    pass


def maybe(flag, other=None):
    if flag:
        return 'true'
    if other is None:
        return 'none'
    return other


print(second('a', 'b'))
show()
show('p')
show('p', 'q', 'r')
show(y='Y')
show('p', z='Z')
show(z='Z', x='X')
defaults('a')
defaults('a', 'x')
defaults('a', 'x', 'y')
defaults('a', c='z')
defaults(c='c', a='a')
defaults(b='bb', a='aa')
kwonly('a', x='x')
kwonly('a', y='yy', x='xx')
kwonly(x='x', a='aa')
posonly('a', 'b', 'c')
posonly('a', 'b', c='c')
rest('a')
rest('a', 'b')
rest('a', 'b', 'c')
options('a')
options('a', y='Y')
options('a', z='Z', y='Y')
everything('a', d='d')
everything('a', 'bb', 'cc', 'extra1', 'extra2', d='dd', e='ee', y='yy')
everything('a', c='cc', d='d', x='X', z='Z')

args = ('p', 'q')
show(*args)
show('first', *args)
show(*args, z='last')
show(*args, **{'z': 'Z'})
kws = {'y': 'Y', 'z': 'Z'}
show('first', **kws)
show(**kws, x='X')
show(*(), **{})
rest(*args)
rest('a', *args, *args)
options('a', **kws)

if nothing() is None:
    print('returns None')
print(maybe(True))
print(maybe(False))
print(maybe(False, 'other'))
print(maybe(None, 'o'))
print(maybe('', 'o'))
print(maybe('s'))
print(second(*args))
if print is print:
    print('same')
if not show:
    print('never')
