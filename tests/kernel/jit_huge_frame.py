import collections
import resource

# A function of 20,000 locals compiles in memory that follows its size: the
# frame states of one frame share their locals. A namedtuple of 20,000 fields
# has such a __new__ (test_collections' test_large_size); in the jit lanes it
# compiles at its first call. Copying the locals into every exit's state took
# 3 GB here, CPython takes a few MB.
names = [f"f{i}" for i in range(20_000)]
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
Big = collections.namedtuple("Big", names)
for _ in range(3):
    b = Big(*range(len(names)))
grown = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss - before
print(b == tuple(range(len(names))), b.f19999, grown < 512 * 1024)
