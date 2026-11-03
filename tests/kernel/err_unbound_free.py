def f():
    def g():
        return x
    r = g()
    x = 'late'
    return r


f()
