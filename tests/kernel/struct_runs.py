# struct packs and unpacks a run of items at a time: every code in both byte
# orders and native mode, repeat counts, padding, the edges of each range,
# and the buffer kinds and offsets of the *_from and *_into calls.
import random
import struct

random.seed(5)
FORMATS = ['<I', '>I', '!I', '<i', '>i', '<H', '>h', 'B', 'b', '<Q', '>q', '<d', '>d', '<f', '>e', '?', 'c', '5s', '5p',
           '0p', '1p', '0s', '<IHBx', '>3I2h', '@iqh', '@ccP', '=lL', '<?3c', '!20I', '<2d3f', 'xxi', '@b i', '<10s2B',
           '@nN', '>4H', '<3q', '@hd', '=e2x?']
EDGES = {'b': (-128, 127), 'B': (0, 255), 'h': (-32768, 32767), 'H': (0, 65535), 'i': (-2**31, 2**31 - 1),
         'I': (0, 2**32 - 1), 'l': (-2**63, 2**63 - 1), 'L': (0, 2**64 - 1), 'q': (-2**63, 2**63 - 1),
         'Q': (0, 2**64 - 1), 'n': (-2**63, 2**63 - 1), 'N': (0, 2**64 - 1), 'P': (0, 2**64 - 1)}


def codes(fmt):
    out, count = [], ''
    for c in fmt.lstrip('<>!=@'):
        if c.isdigit():
            count += c
        elif c in ' x':
            count = ''
        else:
            out += [c] if c in 'sp' else [c] * int(count or 1)
            count = ''
    return out


def value(c, edge):
    if c in EDGES:
        low, high = EDGES[c]
        return (low, high, 0, 1, -1 if low else 2)[edge] if edge < 5 else random.randint(low, high)
    if c in 'dfe':
        return (0.5, -2.25, 1e10, float('inf'), -0.0)[edge] if edge < 5 else random.uniform(-100, 100)
    if c == '?':
        return (0, 1, [], 'x', None)[edge] if edge < 5 else random.random() < 0.5
    if c == 'c':
        return bytes([random.randrange(256)])
    return random.randbytes(random.randrange(8))


for fmt in FORMATS:
    s = struct.Struct(fmt)
    for edge in range(8):
        args = [value(c, edge) for c in codes(fmt)]
        try:
            data = s.pack(*args)
        except (struct.error, OverflowError) as e:
            print(fmt, edge, type(e).__name__, e)
            continue
        back = s.unpack(data)
        print(fmt, s.size, data.hex(), back)
        assert struct.unpack(fmt, bytearray(data)) == back and s.unpack(memoryview(data)) == back
        buf = bytearray(b'\xaa' * 3 + data + data)
        assert s.unpack_from(buf, 3) == back and s.unpack_from(memoryview(buf), -len(data)) == back
        assert struct.unpack_from(fmt, buf, offset=3 + len(data)) == back
        if s.size:
            assert list(s.iter_unpack(data * 3)) == [back] * 3
        target = bytearray(len(data) + 4)
        s.pack_into(target, 2, *args)
        assert bytes(target[2:2 + len(data)]) == data, fmt
        struct.pack_into(fmt, target, -len(data), *args)
        assert bytes(target[-len(data):]) == data if data else True
big = struct.Struct('!%dI' % 1000)
words = [random.getrandbits(32) for _ in range(1000)]
packed = big.pack(*words)
print(len(packed), packed[:12].hex(), big.unpack(packed) == tuple(words), sum(struct.unpack('<1000I', packed)) % 997)
for fmt, args in (('<h', (40000,)), ('<I', (-1,)), ('B', (1.5,)), ('<d', ('x',)), ('c', (b'ab',)), ('5s', ('x',)),
                  ('<2I', (1,)), ('<I', ())):
    try:
        struct.pack(fmt, *args)
    except struct.error as e:
        print(fmt, 'error', e)
for fmt, data in (('<I', b'abc'), ('<I', b'abcde'), ('!20I', b'\0' * 79)):
    try:
        struct.unpack(fmt, data)
    except struct.error as e:
        print(fmt, 'error', e)

# Compare the error at either edge of each integer range in both packing paths.
for mode in ('', '@', '=', '<', '>', '!'):
    for code in ('lLqQnNP' if mode in ('', '@') else 'lLqQ'):
        bits = 8 * struct.calcsize(mode + code)
        low = -(1 << (bits - 1)) if code in 'lqnP' else 0
        high = (1 << (bits - (code in 'lqn'))) - 1
        for value in (low - 1, high + 1):
            for suffix, tail in (('', ()), ('b', (0,))):
                fmt = mode + code + suffix
                try:
                    struct.pack(fmt, value, *tail)
                except struct.error as e:
                    print(fmt, value, type(e).__name__, e)
                else:
                    raise AssertionError((fmt, value))


class Indexed:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


for value in (-2**63, -1, 2**64 - 1, Indexed(-2**63 - 1), Indexed(2**64)):
    try:
        print('@P', struct.pack('@P', value).hex())
    except (struct.error, OverflowError) as e:
        print('@P', type(e).__name__, e)
