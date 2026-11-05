import array
import hashlib


class Truthless:
    """A buffer whose truth value is an error, as an ndarray's is."""
    def __init__(self, data):
        self.view = memoryview(data)

    def __bool__(self):
        raise ValueError('ambiguous')

    def __buffer__(self, flags):
        return self.view


data = b'abcdefghij' * 7
for name in ('md5', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512', 'sha3_256', 'sha3_512', 'blake2b', 'blake2s'):
    ctor = getattr(hashlib, name)
    print(name, ctor(Truthless(data)).hexdigest() == ctor(data).hexdigest(), ctor(b'').hexdigest() == ctor().hexdigest(),
          ctor(bytearray(data)).hexdigest() == ctor(data).hexdigest(), ctor(memoryview(data)).hexdigest()[:8])
