import random

r = random.Random(12345)
print([r.random() for _ in range(5)])
print([r.getrandbits(k) for k in (1, 5, 31, 32, 33, 64, 100, 200)])
print(r.getrandbits(0), [random.Random(s).random() for s in (0, 1, -7, 2**40, 2**100, 'abc', b'xyz', 1.5, None.__class__ and 3)])
random.seed(42)
print(random.random(), random.randint(1, 100), random.choice('abcdef'), random.randrange(10**20), random.uniform(1, 2))
x = list(range(20))
random.shuffle(x)
print(x, random.sample(range(100), 5), random.gauss(0, 1), random.choices('abc', k=5), random.expovariate(1.5))
s = r.getstate()
a = [r.random() for _ in range(700)]
r.setstate(s)
print(a == [r.random() for _ in range(700)], len(s), s[-1])
for bad in (lambda: r.setstate(s[:5]), lambda: r.setstate([1]), lambda: r.getrandbits(-1), lambda: r.setstate(s[:-1] + (625,))):
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)
print(random.Random('seed').getrandbits(64), random.Random(b'seed').random(), random.randbytes(4) is not None)
import hashlib
import _md5

for name in ('md5', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512'):
    h = getattr(hashlib, name)(b'abc' * 100)
    h.update(b'x')
    print(name, h.hexdigest(), h.digest_size, h.block_size, h.name, h.copy().hexdigest() == h.hexdigest())
for name in ('sha3_224', 'sha3_256', 'sha3_384', 'sha3_512', 'blake2b', 'blake2s'):
    h = getattr(hashlib, name)(b'abc' * 100)
    h.update(b'x' * 200)
    print(name, h.hexdigest(), h.digest_size, h.block_size, h.name, h.copy().hexdigest() == h.hexdigest())
for name in ('shake_128', 'shake_256'):
    h = getattr(hashlib, name)(b'abc')
    print(name, h.hexdigest(10), h.digest(300)[-5:].hex(), h.block_size)
print(hashlib.blake2b(b'data', digest_size=20, key=b'k', salt=b'salt', person=b'pers', fanout=2, depth=3, leaf_size=5, node_offset=7,
                      node_depth=1, inner_size=9, last_node=True).hexdigest())
print(hashlib.blake2s(b'data', digest_size=20, key=b'k', salt=b'salt', person=b'pers', fanout=2, depth=3, leaf_size=5, node_offset=7,
                      node_depth=1, inner_size=9, last_node=True).hexdigest())
print(hashlib.blake2b().hexdigest(), hashlib.blake2s(key=b'x' * 32).hexdigest(), hashlib.new('sha256', b'q').hexdigest())
print(sorted(hashlib.algorithms_guaranteed), hashlib.sha256(b'a' * 1000000).hexdigest())
