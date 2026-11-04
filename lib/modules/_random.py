"""_random (Modules/_randommodule.c): the Mersenne Twister MT19937 generator
under random.Random, with CPython's seeding, state layout and output."""

import _lucifer

_N, _M = 624, 397
_MATRIX_A, _UPPER_MASK, _LOWER_MASK = 0x9908b0df, 0x80000000, 0x7fffffff


class Random:
    """Random() -> create a random number generator with its own internal state."""

    __slots__ = ('_state', '__weakref__')

    def __new__(cls, *args, **kwargs):
        if len(args) > 1:
            raise TypeError('Random() expects at most 1 argument.')
        if kwargs and (cls is Random or cls.__init__ is Random.__init__):
            raise TypeError('Random() takes no keyword arguments')
        self = object.__new__(cls)
        self._state = bytearray((_N + 1) * 4)
        self.seed(args[0] if args else None)
        return self

    def _store(self, mt, index):
        """The state bytearray from the words and the index of the next one."""
        self._state[:] = b''.join([word.to_bytes(4, 'little') for word in mt]) + index.to_bytes(4, 'little')

    def _words(self):
        data = bytes(self._state)
        return [int.from_bytes(data[i:i + 4], 'little') for i in range(0, (_N + 1) * 4, 4)]

    def _init_by_array(self, key):
        mt = [0] * _N
        mt[0] = 19650218
        for i in range(1, _N):
            mt[i] = (1812433253 * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i) & 0xffffffff
        i, j = 1, 0
        for _ in range(max(_N, len(key))):
            mt[i] = ((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525)) + key[j] + j) & 0xffffffff
            i += 1
            j += 1
            if i >= _N:
                mt[0] = mt[_N - 1]
                i = 1
            if j >= len(key):
                j = 0
        for _ in range(_N - 1):
            mt[i] = ((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941)) - i) & 0xffffffff
            i += 1
            if i >= _N:
                mt[0] = mt[_N - 1]
                i = 1
        mt[0] = 0x80000000
        self._store(mt, _N)

    def seed(self, n=None, /):
        """seed([n]) -> None.

Defaults to use urandom and falls back to a combination
of the current time and the process identifier."""
        if n is None:
            n = int.from_bytes(_lucifer.os_urandom(32), 'little')
        elif isinstance(n, int):
            n = abs(n) if type(n) is int else abs(int(n))
        else:
            n = hash(n) & ((1 << 64) - 1) if hash(n) < 0 else hash(n)
            n = abs(n)
        key = []
        while n:
            key.append(n & 0xffffffff)
            n >>= 32
        self._init_by_array(key or [0])

    def random(self):
        """random() -> x in the interval [0, 1)."""
        return _lucifer.mt_random(self._state)

    def getrandbits(self, k, /):
        """getrandbits(k) -> x.  Generates an int with k random bits."""
        if k < 0:
            raise ValueError('Cannot convert negative int')
        if k == 0:
            return 0
        state = self._state
        if k <= 32:
            return _lucifer.mt_next32(state) >> (32 - k)
        words = (k - 1) // 32 + 1
        result = 0
        for i in range(words):
            r = _lucifer.mt_next32(state)
            if k < 32:
                r >>= 32 - k
            result |= r << (32 * i)
            k -= 32
        return result

    def getstate(self):
        """getstate() -> tuple containing the current state."""
        return tuple(self._words())

    def setstate(self, state, /):
        """setstate(state) -> None.  Restores generator state."""
        if not isinstance(state, tuple):
            raise TypeError('state vector must be a tuple')
        if len(state) != _N + 1:
            raise ValueError('state vector is the wrong size')
        for element in state:
            if not isinstance(element, int):
                raise TypeError('state vector items must be integers')
            if not 0 <= element < (1 << 32) and element is not state[-1]:
                raise OverflowError('Python int too large to convert to C unsigned long')
        index = state[-1]
        if not 0 <= index <= _N:
            raise ValueError('invalid state')
        self._store(state[:-1], index)
