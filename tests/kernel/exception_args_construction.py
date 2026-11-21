def hooked_subclass(base):
    class Hooked(base):
        def __setattr__(self, name, value):
            print('hook', name)
            object.__setattr__(self, name, value)

    return Hooked


for base, args, kwargs in (
    (Exception, (), {}),
    (Exception, (1, 2), {}),
    (NameError, ('missing',), {'name': 'item'}),
    (AttributeError, ('missing',), {'name': 'item'}),
    (ImportError, ('missing',), {'name': 'pkg'}),
    (OSError, (2, 'missing', 'file'), {}),
    (ExceptionGroup, ('group', [ValueError('member')]), {}),
):
    exc = hooked_subclass(base)(*args, **kwargs)
    print(base.__name__, exc.args)


Hooked = hooked_subclass(Exception)


class CustomInit(Hooked):
    def __init__(self, *args):
        print('before base init', self.args)
        super().__init__('updated')


exc = CustomInit('original')
print('after base init', exc.args)
exc.args = ('assigned',)
print('after assignment', exc.args)


class CustomOSError(hooked_subclass(OSError)):
    def __init__(self, *args):
        print('before OSError init', self.args)
        super().__init__(*args)


exc = CustomOSError(2, 'missing', 'file')
print('after OSError init', exc.args, exc.filename)


class Shadowed(Exception):
    @property
    def args(self):
        return 'shadowed'


exc = Shadowed('stored')
print(exc.args, BaseException.args.__get__(exc))
