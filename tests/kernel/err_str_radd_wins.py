class R:
    def __radd__(self, o):
        return 'radd ' + o
print('a' + R())
class Q:
    def __rmul__(self, o):
        return 'rmul ' + o
print('a' * Q())
print('a' + 1)
