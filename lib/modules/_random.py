"""_random (Modules/_randommodule.c): the Mersenne Twister MT19937 generator
under random.Random, with CPython's seeding, state layout and output."""

import _lucifer

_N, _M = 624, 397
_MATRIX_A, _UPPER_MASK, _LOWER_MASK = 0x9908b0df, 0x80000000, 0x7fffffff


class Random:
    """Random() -> create a random number generator with its own internal state."""

    __slots__ = ('_mt', '_index', '__weakref__')

    def __new__(cls, *args, **kwargs):
        if len(args) > 1:
            raise TypeError('Random() expects at most 1 argument.')
        if kwargs and (cls is Random or cls.__init__ is Random.__init__):
            raise TypeError('Random() takes no keyword arguments')
        self = object.__new__(cls)
        self._mt = [0] * _N
        self._index = _N + 1
        self.seed(args[0] if args else None)
        return self

    def _init_genrand(self, s):
        mt = self._mt
        mt[0] = s & 0xffffffff
        for i in range(1, _N):
            mt[i] = (1812433253 * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i) & 0xffffffff
        self._index = _N

    def _init_by_array(self, key):
        self._init_genrand(19650218)
        mt = self._mt
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
        self._index = _N

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

    def _genrand(self):
        mt = self._mt
        if self._index >= _N:
            for kk in range(_N - _M):
                y = (mt[kk] & _UPPER_MASK) | (mt[kk + 1] & _LOWER_MASK)
                mt[kk] = mt[kk + _M] ^ (y >> 1) ^ (_MATRIX_A if y & 1 else 0)
            for kk in range(_N - _M, _N - 1):
                y = (mt[kk] & _UPPER_MASK) | (mt[kk + 1] & _LOWER_MASK)
                mt[kk] = mt[kk + (_M - _N)] ^ (y >> 1) ^ (_MATRIX_A if y & 1 else 0)
            y = (mt[_N - 1] & _UPPER_MASK) | (mt[0] & _LOWER_MASK)
            mt[_N - 1] = mt[_M - 1] ^ (y >> 1) ^ (_MATRIX_A if y & 1 else 0)
            self._index = 0
        y = mt[self._index]
        self._index += 1
        y ^= y >> 11
        y ^= (y << 7) & 0x9d2c5680
        y ^= (y << 15) & 0xefc60000
        y ^= y >> 18
        return y

    def random(self):
        """random() -> x in the interval [0, 1)."""
        a = self._genrand() >> 5
        b = self._genrand() >> 6
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0)

    def getrandbits(self, k, /):
        """getrandbits(k) -> x.  Generates an int with k random bits."""
        if k < 0:
            raise ValueError('Cannot convert negative int')
        if k == 0:
            return 0
        if k <= 32:
            return self._genrand() >> (32 - k)
        words = (k - 1) // 32 + 1
        result = 0
        for i in range(words):
            r = self._genrand()
            if k < 32:
                r >>= 32 - k
            result |= r << (32 * i)
            k -= 32
        return result

    def getstate(self):
        """getstate() -> tuple containing the current state."""
        return tuple(self._mt) + (self._index,)

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
        self._mt = list(state[:-1])
        self._index = index
