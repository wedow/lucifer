print('before')
class A:
    def __radd__(self, other):
        return NotImplemented
1 + A()
