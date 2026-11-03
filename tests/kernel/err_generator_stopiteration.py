def leaks():
    yield 1
    next(iter(()))


print(list(leaks()))
