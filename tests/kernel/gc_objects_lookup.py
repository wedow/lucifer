import collections
import gc

# Every object gc.get_objects() returns answers attribute lookups, including
# runtime-internal ones such as a deque's storage and a memoryview's buffer.
d = collections.deque([1, 2, 3])
m = memoryview(b"abc")
looked = 0
for o in gc.get_objects():
    t = type(o)
    getattr(o, "__class__", None)
    getattr(o, "no_such_attribute", None)
    isinstance(o, (int, collections.deque))
    t.__mro__
    t.__name__
    looked += 1
print(looked > 0, len(d), m.tobytes())
