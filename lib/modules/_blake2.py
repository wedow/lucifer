"""_blake2 (Modules/_blake2/): BLAKE2b and BLAKE2s (RFC 7693) with the
parameter block of the reference implementation: key, salt, personalization
and tree hashing."""

_SIGMA = (
    (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    (14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3),
    (11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4),
    (7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8),
    (9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13),
    (2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9),
    (12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11),
    (13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10),
    (6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5),
    (10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0))

_IV_B = (0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
         0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179)
_IV_S = (0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19)

BLAKE2B_MAX_DIGEST_SIZE, BLAKE2B_MAX_KEY_SIZE, BLAKE2B_SALT_SIZE, BLAKE2B_PERSON_SIZE = 64, 64, 16, 16
BLAKE2S_MAX_DIGEST_SIZE, BLAKE2S_MAX_KEY_SIZE, BLAKE2S_SALT_SIZE, BLAKE2S_PERSON_SIZE = 32, 32, 8, 8
_GIL_MINSIZE = 2048


class _Blake2:
    __slots__ = ('_h', '_t', '_buffer', '_last_node', '_digest_size', '_final', '__weakref__')
    _word = 8
    _name = ''

    def __init__(self, data=b'', /, *, digest_size=None, key=b'', salt=b'', person=b'', fanout=1,
                 depth=1, leaf_size=0, node_offset=0, node_depth=0, inner_size=0, last_node=False,
                 usedforsecurity=True):
        cls = type(self)
        name, max_digest, max_key = cls._name, cls._max_digest, cls._max_key
        if digest_size is None:
            digest_size = max_digest
        if not 1 <= digest_size <= max_digest:
            raise ValueError('digest_size for %s must be between 1 and %d bytes, here it is %d'
                             % (name, max_digest, digest_size))
        key, salt, person = bytes(key), bytes(salt), bytes(person)
        if len(key) > max_key:
            raise ValueError('maximum key length is %d bytes' % max_key)
        if len(salt) > cls._salt_size:
            raise ValueError('maximum salt length is %d bytes' % cls._salt_size)
        if len(person) > cls._person_size:
            raise ValueError('maximum person length is %d bytes' % cls._person_size)
        if not 0 <= fanout <= 255:
            raise ValueError('fanout must be between 0 and 255')
        if not 1 <= depth <= 255:
            raise ValueError('depth must be between 1 and 255')
        if not 0 <= leaf_size <= 0xffffffff:
            raise OverflowError('leaf_size is too large')
        if node_offset < 0 or node_offset >= 1 << (8 * cls._offset_size):
            raise OverflowError('node_offset is too large')
        if not 0 <= node_depth <= 255:
            raise ValueError('node_depth must be between 0 and 255')
        if not 0 <= inner_size <= max_digest:
            raise ValueError('inner_size must be between 0 and is %d' % max_digest)
        word = cls._word
        block = bytearray(8 * word)
        block[0], block[1], block[2], block[3] = digest_size, len(key), fanout, depth
        block[4:8] = leaf_size.to_bytes(4, 'little')
        end = 8 + cls._offset_size
        block[8:end] = node_offset.to_bytes(cls._offset_size, 'little')
        block[end], block[end + 1] = node_depth, inner_size
        salt_at = 4 * word
        block[salt_at:salt_at + len(salt)] = salt
        person_at = 6 * word if word == 8 else 24
        block[person_at:person_at + len(person)] = person
        self._h = [cls._iv[i] ^ int.from_bytes(block[i * word:(i + 1) * word], 'little') for i in range(8)]
        self._t = 0
        self._buffer = b''
        self._last_node = bool(last_node)
        self._digest_size = digest_size
        if key:
            self._buffer = key.ljust(cls._block, b'\0')
        if data is not None:
            self.update(data)

    @property
    def name(self):
        return self._name

    @property
    def block_size(self):
        return self._block

    @property
    def digest_size(self):
        return self._digest_size

    def _compress(self, block, last):
        word = self._word
        bits = 8 * word
        mask = (1 << bits) - 1
        rounds = self._rounds
        r1, r2, r3, r4 = self._rotations
        m = [int.from_bytes(block[i * word:(i + 1) * word], 'little') for i in range(16)]
        h = self._h
        v = h + list(self._iv)
        v[12] ^= self._t & mask
        v[13] ^= (self._t >> bits) & mask
        if last:
            v[14] ^= mask
            if self._last_node:
                v[15] ^= mask

        def g(a, b, c, d, x, y):
            v[a] = (v[a] + v[b] + x) & mask
            d_ = v[d] ^ v[a]
            v[d] = ((d_ >> r1) | (d_ << (bits - r1))) & mask
            v[c] = (v[c] + v[d]) & mask
            b_ = v[b] ^ v[c]
            v[b] = ((b_ >> r2) | (b_ << (bits - r2))) & mask
            v[a] = (v[a] + v[b] + y) & mask
            d_ = v[d] ^ v[a]
            v[d] = ((d_ >> r3) | (d_ << (bits - r3))) & mask
            v[c] = (v[c] + v[d]) & mask
            b_ = v[b] ^ v[c]
            v[b] = ((b_ >> r4) | (b_ << (bits - r4))) & mask

        for i in range(rounds):
            s = _SIGMA[i % 10]
            g(0, 4, 8, 12, m[s[0]], m[s[1]])
            g(1, 5, 9, 13, m[s[2]], m[s[3]])
            g(2, 6, 10, 14, m[s[4]], m[s[5]])
            g(3, 7, 11, 15, m[s[6]], m[s[7]])
            g(0, 5, 10, 15, m[s[8]], m[s[9]])
            g(1, 6, 11, 12, m[s[10]], m[s[11]])
            g(2, 7, 8, 13, m[s[12]], m[s[13]])
            g(3, 4, 9, 14, m[s[14]], m[s[15]])
        self._h = [h[i] ^ v[i] ^ v[i + 8] for i in range(8)]

    def update(self, data, /):
        """Update this hash object's state with the provided bytes-like object."""
        if isinstance(data, str):
            raise TypeError('Strings must be encoded before hashing')
        buffer = self._buffer + bytes(memoryview(data))
        block = self._block
        while len(buffer) > block:
            self._t += block
            self._compress(buffer[:block], False)
            buffer = buffer[block:]
        self._buffer = buffer

    def _finish(self):
        saved = (list(self._h), self._t)
        buffer = self._buffer
        self._t += len(buffer)
        self._compress(buffer.ljust(self._block, b'\0'), True)
        out = b''.join(x.to_bytes(self._word, 'little') for x in self._h)[:self._digest_size]
        self._h, self._t = saved
        return out

    def digest(self):
        """Return the digest value as a bytes object."""
        return self._finish()

    def hexdigest(self):
        """Return the digest value as a string of hexadecimal digits."""
        return self._finish().hex()

    def copy(self):
        """Return a copy of the hash object."""
        other = object.__new__(type(self))
        other._h, other._t, other._buffer = list(self._h), self._t, self._buffer
        other._last_node, other._digest_size = self._last_node, self._digest_size
        return other


class blake2b(_Blake2):
    """Return a new BLAKE2b hash object."""
    __slots__ = ()
    _name = 'blake2b'
    _word = 8
    _block = 128
    _max_digest, _max_key, _salt_size, _person_size, _offset_size = 64, 64, 16, 16, 8
    _iv = _IV_B
    _rounds = 12
    _rotations = (32, 24, 16, 63)

    SALT_SIZE = 16
    PERSON_SIZE = 16
    MAX_KEY_SIZE = 64
    MAX_DIGEST_SIZE = 64


class blake2s(_Blake2):
    """Return a new BLAKE2s hash object."""
    __slots__ = ()
    _name = 'blake2s'
    _word = 4
    _block = 64
    _max_digest, _max_key, _salt_size, _person_size, _offset_size = 32, 32, 8, 8, 6
    _iv = _IV_S
    _rounds = 10
    _rotations = (16, 12, 8, 7)

    SALT_SIZE = 8
    PERSON_SIZE = 8
    MAX_KEY_SIZE = 32
    MAX_DIGEST_SIZE = 32


blake2b.__module__ = blake2s.__module__ = '_blake2'
