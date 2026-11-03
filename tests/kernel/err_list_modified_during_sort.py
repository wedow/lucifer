data = [3, 1, 2]


def grow(x):
    data.append(0)
    return x


data.sort(key=grow)
