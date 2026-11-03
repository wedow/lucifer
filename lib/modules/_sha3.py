"""_sha3 (Modules/_sha3/): SHA-3 and SHAKE over the Keccak-f[1600] permutation."""

_MASK = (1 << 64) - 1
_ROUND_CONSTANTS = (
    0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000,
    0x000000000000808b, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008a, 0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
    0x000000008000808b, 0x800000000000008b, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008)
_ROTATIONS = (0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14)
implementation = 'lucifer'
_GIL_MINSIZE = 2048


def _keccak_f(a):
    for rc in _ROUND_CONSTANTS:
        c = [a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20] for x in range(5)]
        for x in range(5):
            d = c[(x - 1) % 5] ^ (((c[(x + 1) % 5] << 1) | (c[(x + 1) % 5] >> 63)) & _MASK)
            for y in range(0, 25, 5):
                a[x + y] ^= d
        b = [0] * 25
        for x in range(5):
            for y in range(5):
                r = _ROTATIONS[x + 5 * y]
                v = a[x + 5 * y]
                b[y + 5 * ((2 * x + 3 * y) % 5)] = ((v << r) | (v >> (64 - r))) & _MASK if r else v
        for y in range(0, 25, 5):
            for x in range(5):
                a[x + y] = b[x + y] ^ ((~b[(x + 1) % 5 + y]) & b[(x + 2) % 5 + y])
        a[0] ^= rc


class _Keccak:
    __slots__ = ('_state', '_buffer', '__weakref__')
    _rate = 0
    _suffix = 0x06
    name = ''
    digest_size = 0

    def __init__(self, data=b'', /, *, usedforsecurity=True):
        self._state = [0] * 25
        self._buffer = b''
        if data:
            self.update(data)

    @property
    def block_size(self):
        return self._rate

    @property
    def _capacity_bits(self):
        return 1600 - 8 * self._rate

    def _absorb(self, block):
        state = self._state
        for i in range(len(block) // 8):
            state[i] ^= int.from_bytes(block[8 * i:8 * i + 8], 'little')
        _keccak_f(state)

    def update(self, data, /):
        """Update this hash object's state with the provided bytes-like object."""
        if isinstance(data, str):
            raise TypeError('Strings must be encoded before hashing')
        buffer = self._buffer + bytes(memoryview(data))
        rate = self._rate
        while len(buffer) >= rate:
            self._absorb(buffer[:rate])
            buffer = buffer[rate:]
        self._buffer = buffer

    def _squeeze(self, length):
        state = list(self._state)
        rate = self._rate
        pad = bytearray(self._buffer.ljust(rate, b'\0'))
        pad[len(self._buffer)] ^= self._suffix
        pad[rate - 1] ^= 0x80
        for i in range(rate // 8):
            state[i] ^= int.from_bytes(pad[8 * i:8 * i + 8], 'little')
        _keccak_f(state)
        out = b''
        while True:
            out += b''.join(state[i].to_bytes(8, 'little') for i in range(rate // 8))
            if len(out) >= length:
                return out[:length]
            _keccak_f(state)

    def copy(self):
        """Return a copy of the hash object."""
        other = object.__new__(type(self))
        other._state, other._buffer = list(self._state), self._buffer
        return other


class _Sha3(_Keccak):
    __slots__ = ()

    def digest(self):
        """Return the digest value as a bytes object."""
        return self._squeeze(self.digest_size)

    def hexdigest(self):
        """Return the digest value as a string of hexadecimal digits."""
        return self.digest().hex()


class _Shake(_Keccak):
    __slots__ = ()
    _suffix = 0x1f
    digest_size = 0

    def digest(self, length):
        """Return the digest value as a bytes object."""
        if length < 0:
            raise ValueError('negative digest length')
        return self._squeeze(length)

    def hexdigest(self, length):
        """Return the digest value as a string of hexadecimal digits."""
        return self.digest(length).hex()


def _sha3(name, rate, size):
    return type(name, (_Sha3,), {'__slots__': (), '_rate': rate, 'name': name, 'digest_size': size,
                                 '__module__': '_sha3', '__doc__': 'Return a new SHA3 hash object.'})


def _shake(name, rate):
    return type(name, (_Shake,), {'__slots__': (), '_rate': rate, 'name': name,
                                  '__module__': '_sha3', '__doc__': 'Return a new SHAKE hash object.'})


sha3_224 = _sha3('sha3_224', 144, 28)
sha3_256 = _sha3('sha3_256', 136, 32)
sha3_384 = _sha3('sha3_384', 104, 48)
sha3_512 = _sha3('sha3_512', 72, 64)
shake_128 = _shake('shake_128', 168)
shake_256 = _shake('shake_256', 136)
