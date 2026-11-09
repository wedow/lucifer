# The C _pickle: what it shares with the runtime (PickleBuffer, set update
# with any hashable keys, _PyBytesWriter output, interned identifiers) and the
# behaviour CPython's C unpickler alone has.
import io
import pickle
import _pickle


class K:
    def __init__(self, value):
        self.value = value

    def __reduce__(self):
        return K, (self.value,)


print(pickle.Pickler is _pickle.Pickler, type(_pickle.PickleBuffer).__name__)
y = set()
y.add(K(y))
x = pickle.loads(pickle.dumps(y, 4))
print(type(x).__name__, len(x), next(iter(x)).value is x)
data = {'a': [1, 2.5, 'x' * 600, (None, True)], 'b': {3, 4}, 'c': bytearray(b'z' * 1000)}
for proto in range(6):
    print(proto, pickle.loads(pickle.dumps(data, proto)) == data)
buffers = []
blob = pickle.dumps(pickle.PickleBuffer(b'abc'), 5, buffer_callback=buffers.append)
print(bytes(pickle.loads(blob, buffers=buffers)), bytes(buffers[0].raw()))
for bad in (b'B', b'(', b'0'):
    try:
        pickle.loads(bad)
    except pickle.UnpicklingError as e:
        print('UnpicklingError', e)
try:
    pickle.dumps([lambda: 0])
except pickle.PicklingError as e:
    print(type(e).__name__, e.__notes__)
p = _pickle.Pickler(io.BytesIO())
p.bin = True
print(p.bin, p.fast)
