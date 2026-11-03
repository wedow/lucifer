# Run without -I, once per PYTHONHASHSEED (tools/kernelcheck): the first line is
# the randomization flag, the rest depend on the seed.
import sys

print(sys.flags.hash_randomization)
print(hash("abc"), hash(b"abc"), hash(""), hash("a" * 100))
print(list({"a", "b", "c", "d"}))
print(list({"spam", "eggs", "ham", "toast", "bacon"}))
print(sys.hash_info.algorithm, sys.hash_info.width, sys.hash_info.seed_bits)
