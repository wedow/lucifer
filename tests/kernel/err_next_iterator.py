class Broken:
    def __iter__(self):
        return self

    def __next__(self):
        raise RuntimeError('inside next')


for x in Broken():
    print(x)
