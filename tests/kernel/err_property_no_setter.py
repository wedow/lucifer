class A:
    p = property(lambda self: 'p')


A().p = 'x'
