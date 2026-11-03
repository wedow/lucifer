def f():
    x = 'a'
    def g():
        return x
    del x
    del x


f()
