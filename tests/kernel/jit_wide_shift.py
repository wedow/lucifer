# Packing 64-bit integers takes shifts whose results exceed a signed word.
# jitcheck also checks that repeated packing stops leaving compiled code.
import sys
from array import array

n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
values = array('q', range(n))
print(len(values), values[0], values[-1])
