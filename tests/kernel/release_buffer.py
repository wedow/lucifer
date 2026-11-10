# PEP 688: a class's __release_buffer__ runs when a buffer its __buffer__
# exported is released, with the memoryview __buffer__ returned.
class B:
    def __init__(self):
        self.out = []
        self.data = bytearray(b'abcdef')

    def __buffer__(self, flags):
        mv = memoryview(self.data)
        self.out.append(mv)
        print('get', flags)
        return mv

    def __release_buffer__(self, view):
        print('release', view is self.out[-1], view.tobytes())
        self.out.pop()


b = B()
v = memoryview(b)
print(bytes(v[:3]), len(b.out))
v.release()
print(len(b.out))
v = memoryview(b)
del v
print(len(b.out))
with memoryview(b) as v:
    print(v[1])
print(len(b.out), bytes(b), len(b.out))


class C(B):
    def __release_buffer__(self, view):
        raise ValueError('boom')


import sys
seen = []
sys.unraisablehook = lambda u: seen.append(type(u.exc_value).__name__)
c = C()
memoryview(c).release()
print(seen)


# A native exporter's subclass that adds __release_buffer__ still releases
# through the native base: the bytearray can resize afterwards.
class BA(bytearray):
    def __release_buffer__(self, view):
        pass


ba = BA(b'xyz')
memoryview(ba).release()
ba.extend(b'!')
print(ba)
