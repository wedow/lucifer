def key(x):
    if x == 2:
        raise ValueError('no two')
    return x


items = [3, 2, 1]
print(sorted([1, 0]))
items.sort(key=key)
