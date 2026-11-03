def f():
    def g():
        nonlocal x
        del x
    x = 'a'
    g()
    g()


f()
