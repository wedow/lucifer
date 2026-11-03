d = {1: 1, 2: 2, 3: 3}
it = iter(d)
next(it)
del d[3]
d[4] = 4
next(it)
