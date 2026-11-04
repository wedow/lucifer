import pickle
from pickle import PickleBuffer

print(PickleBuffer.__module__, PickleBuffer.__name__)
b = PickleBuffer(b'abcdef')
print(bytes(memoryview(b)), bytes(b.raw()), b.raw().format, b.raw().readonly)
ba = bytearray(b'xyz')
pb = PickleBuffer(ba)
m = pb.raw()
m[0] = 65
print(ba, m.readonly)
a = memoryview(bytes(12)).cast('i')
print(len(PickleBuffer(a).raw()), PickleBuffer(a).raw().format, memoryview(PickleBuffer(a)).format)
nc = PickleBuffer(memoryview(b'0123456789')[::2])
try:
    nc.raw()
except BufferError as e:
    print('BufferError', e)
try:
    PickleBuffer(5)
except TypeError as e:
    print('TypeError', e)
pb.release()
try:
    pb.raw()
except ValueError as e:
    print('ValueError', e)
try:
    memoryview(pb)
except ValueError as e:
    print('ValueError', e)

out = []
data = pickle.dumps([PickleBuffer(bytearray(b'oob'))], protocol=5, buffer_callback=out.append)
print(len(out), bytes(out[0].raw()))
print(pickle.loads(data, buffers=[bytearray(b'new')]))
print(pickle.loads(pickle.dumps(PickleBuffer(b'inband'), protocol=5)))
try:
    pickle.dumps(PickleBuffer(b'x'), protocol=4)
except pickle.PicklingError as e:
    print('PicklingError', e)
